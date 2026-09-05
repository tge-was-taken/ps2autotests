#include <common-ee.h>
#include <kernel.h>
#include <string.h>

// What the processor state looks like inside an interrupt handler, and what
// happens when a second cause is raised while the first one is being handled.

#define MARK_LIMIT 32

static volatile int marks[MARK_LIMIT];
static volatile int markCount;

static void mark(int value) {
	if (markCount < MARK_LIMIT) {
		marks[markCount] = value;
		markCount++;
	}
}

static void printMarks(const char *what) {
	printf("  %-32s", what);
	for (int i = 0; i < markCount; ++i) {
		printf(" %d", marks[i]);
	}
	printf("\n");
}

static u32 readStatus() {
	u32 status;
	asm volatile ("mfc0 %0, $12\n" "sync.p\n" : "=r"(status));
	return status;
}

static void printStatus(const char *what, u32 status) {
	printf("  %-24s %08x  ie %d exl %d erl %d eie %d masks %02x\n", what, status,
	       status & 1, (status >> 1) & 1, (status >> 2) & 1, (status >> 16) & 1,
	       (status >> 10) & 0x3F);
}

// The two timers this uses, and the causes they raise.
static volatile u32 *const timer2 = (volatile u32 *)0x10001000;
static volatile u32 *const timer3 = (volatile u32 *)0x10001800;
static const int cause2 = 11;
static const int cause3 = 12;

static void startTimer(volatile u32 *timer, u32 compare) {
	timer[4] = 0;
	timer[0] = 0;
	timer[8] = compare;
	// The slowest clock, so the second interrupt is far enough away that the
	// code which started the timer gets to run in between.
	timer[4] = 3 | (1 << 7);
}

static void stopTimer(volatile u32 *timer, int cause) {
	timer[4] = 0;
	*(volatile u32 *)0x1000F000 = 1u << cause;
}

static void spin(int iterations) {
	for (volatile int i = 0; i < iterations; ++i) {
		continue;
	}
}

static volatile u32 statusInsideHandler;
static volatile u32 maskInsideHandler;
static volatile u32 statInsideHandler;

// The timer holds its interrupt up until the flags in its mode register are
// written back, so every handler here does that first.
static void acknowledgeTimer(volatile u32 *timer, int cause) {
	timer[4] = 3 << 10;
	iDisableIntc(cause);
}

static int recordingHandler(int cause) {
	acknowledgeTimer(timer3, cause3);
	statusInsideHandler = readStatus();
	maskInsideHandler = *(volatile u32 *)0x1000F010;
	statInsideHandler = *(volatile u32 *)0x1000F000;
	mark(1);
	return 0;
}

// What a handler sees of the processor and of the controller it was called
// from.
static void testStateInsideHandler() {
	printf("The state a handler runs in:\n");
	printStatus("outside", readStatus());

	markCount = 0;
	statusInsideHandler = 0;
	const s32 id = AddIntcHandler(cause3, &recordingHandler, -1);
	EnableIntc(cause3);
	startTimer(timer3, 0x400);
	spin(60000);
	stopTimer(timer3, cause3);
	DisableIntc(cause3);
	if (id >= 0) {
		RemoveIntcHandler(cause3, id);
	}

	printf("  the handler ran %d times\n", markCount);
	printStatus("inside", statusInsideHandler);
	printf("  the mask read %08x and the status read %08x from inside\n",
	       maskInsideHandler, statInsideHandler);
}

// A handler that busy waits long enough for a second cause to come up.
static volatile int secondCauseSeenInside;

static int slowHandler(int cause) {
	acknowledgeTimer(timer3, cause3);
	mark(1);
	startTimer(timer2, 0x200);
	for (volatile int i = 0; i < 40000; ++i) {
		if ((*(volatile u32 *)0x1000F000 & (1u << cause2)) != 0) {
			secondCauseSeenInside = 1;
			break;
		}
	}
	mark(2);
	return 0;
}

static int secondHandler(int cause) {
	acknowledgeTimer(timer2, cause2);
	mark(3);
	return 0;
}

// Whether the second handler runs inside the first or after it returns.
static void testSecondCauseDuringHandler() {
	printf("A second cause raised inside a handler:\n");

	markCount = 0;
	secondCauseSeenInside = 0;

	const s32 slow = AddIntcHandler(cause3, &slowHandler, -1);
	const s32 second = AddIntcHandler(cause2, &secondHandler, -1);
	EnableIntc(cause3);
	EnableIntc(cause2);

	startTimer(timer3, 0x400);
	spin(200000);
	stopTimer(timer3, cause3);
	stopTimer(timer2, cause2);
	DisableIntc(cause3);
	DisableIntc(cause2);
	if (slow >= 0) {
		RemoveIntcHandler(cause3, slow);
	}
	if (second >= 0) {
		RemoveIntcHandler(cause2, second);
	}

	printf("  the second cause was pending inside the first: %s\n",
	       secondCauseSeenInside ? "yes" : "no");
	printMarks("the order they ran in");
}

// Turning a cause on and off from inside a handler, which the kernel offers a
// separate call for.
static volatile int enableResult;
static volatile int disableResult;

static int togglingHandler(int cause) {
	acknowledgeTimer(timer3, cause3);
	enableResult = iEnableIntc(cause2);
	disableResult = iDisableIntc(cause2);
	mark(1);
	return 0;
}

static void testTogglingFromInside() {
	printf("Turning a cause on and off from inside a handler:\n");

	markCount = 0;
	enableResult = -2;
	disableResult = -2;

	const s32 id = AddIntcHandler(cause3, &togglingHandler, -1);
	EnableIntc(cause3);
	startTimer(timer3, 0x400);
	spin(60000);
	stopTimer(timer3, cause3);
	DisableIntc(cause3);
	if (id >= 0) {
		RemoveIntcHandler(cause3, id);
	}

	printf("  the handler ran %d times, enable %d disable %d\n", markCount,
	       enableResult, disableResult);
}

// The two calls that turn every interrupt off and on, taken outside a handler.
static void testGlobalToggle() {
	printf("Turning every interrupt off and on:\n");

	printStatus("before", readStatus());
	const int disabled = DIntr();
	printStatus("after DIntr", readStatus());
	const int enabled = EIntr();
	printStatus("after EIntr", readStatus());
	printf("  DIntr returned %d, EIntr returned %d\n", disabled, enabled);

	// Nested, since a program that turns them off twice has to turn them on
	// twice as well or not at all.
	DIntr();
	DIntr();
	printStatus("after two DIntr", readStatus());
	EIntr();
	printStatus("after one EIntr", readStatus());
	EIntr();
	printStatus("after two EIntr", readStatus());
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testGlobalToggle();
	testStateInsideHandler();
	testTogglingFromInside();
	testSecondCauseDuringHandler();

	printf("-- TEST END\n");
	return 0;
}
