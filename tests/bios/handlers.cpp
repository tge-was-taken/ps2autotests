#include <common-ee.h>
#include <kernel.h>
#include <string.h>

// The three handler lists the kernel keeps, and the order it walks them in.
// A program that installs two handlers on one cause depends on that order, so
// it is the observable rather than any one handler running.

#define MARK_LIMIT 32

static int marks[MARK_LIMIT];
static int markCount;

static void mark(int value) {
	if (markCount < MARK_LIMIT) {
		marks[markCount] = value;
		markCount++;
	}
}

static void printMarks(const char *what) {
	schedf("  %-30s", what);
	for (int i = 0; i < markCount; ++i) {
		schedf(" %d", marks[i]);
	}
	schedf("\n");
}

static void acknowledgeTimer();

template <int number, unsigned int result>
static int intcHandler(int cause) {
	acknowledgeTimer();
	mark(number);
	return (int)result;
}

template <int number, unsigned int result>
static int dmacHandler(int channel) {
	mark(number);
	return (int)result;
}

static void spin(int iterations) {
	for (volatile int i = 0; i < iterations; ++i) {
		continue;
	}
}

// A timer is the one source a program can raise on its own, so the handlers go
// on its cause.
static const int timerCause = 12;
static volatile u32 *const timerBase = (volatile u32 *)0x10001800;

static void startTimer() {
	timerBase[4] = 0;
	timerBase[0] = 0;
	timerBase[8] = 0x400;
	// The slowest clock, so the second interrupt is far enough away that the
	// code which started the timer gets to run in between.
	timerBase[4] = 3 | (1 << 7);
}

// The two flag bits are cleared by writing them back set.  Writing zero stops
// the timer but leaves it asking, and the kernel takes the interrupt forever.
static void acknowledgeTimer() {
	timerBase[4] = 3 << 10;
}

static void stopTimer() {
	timerBase[4] = 0;
	*(volatile u32 *)0x1000F000 = 1u << timerCause;
}

// Handlers added with a next of minus one, which asks for the end of the list.
static void testIntcOrder() {
	s32 ids[3];

	markCount = 0;
	ids[0] = AddIntcHandler(timerCause, &intcHandler<1, 0>, -1);
	ids[1] = AddIntcHandler(timerCause, &intcHandler<2, 0>, -1);
	ids[2] = AddIntcHandler(timerCause, &intcHandler<3, 0>, -1);
	schedf("Three interrupt handlers, ids %d %d %d\n", ids[0], ids[1], ids[2]);

	EnableIntc(timerCause);
	startTimer();
	spin(60000);
	stopTimer();
	DisableIntc(timerCause);
	printMarks("ran in order");

	for (int i = 0; i < 3; ++i) {
		if (ids[i] >= 0) {
			RemoveIntcHandler(timerCause, ids[i]);
		}
	}
}

// A handler that returns a value, since the kernel is documented as stopping
// the walk on one of them.
static void testReturnValue() {
	s32 ids[3];

	markCount = 0;
	ids[0] = AddIntcHandler(timerCause, &intcHandler<1, 0>, -1);
	ids[1] = AddIntcHandler(timerCause, &intcHandler<2, 1>, -1);
	ids[2] = AddIntcHandler(timerCause, &intcHandler<3, 0>, -1);
	schedf("The middle handler returning one:\n");

	EnableIntc(timerCause);
	startTimer();
	spin(60000);
	stopTimer();
	DisableIntc(timerCause);
	printMarks("ran in order");

	for (int i = 0; i < 3; ++i) {
		if (ids[i] >= 0) {
			RemoveIntcHandler(timerCause, ids[i]);
		}
	}
}

// Removing the middle of three, and removing one twice.
static void testRemove() {
	s32 ids[3];

	markCount = 0;
	ids[0] = AddIntcHandler(timerCause, &intcHandler<1, 0>, -1);
	ids[1] = AddIntcHandler(timerCause, &intcHandler<2, 0>, -1);
	ids[2] = AddIntcHandler(timerCause, &intcHandler<3, 0>, -1);

	schedf("Removing the middle handler: %d, again %d\n",
	       RemoveIntcHandler(timerCause, ids[1]),
	       RemoveIntcHandler(timerCause, ids[1]));

	EnableIntc(timerCause);
	startTimer();
	spin(60000);
	stopTimer();
	DisableIntc(timerCause);
	printMarks("ran in order");

	RemoveIntcHandler(timerCause, ids[0]);
	RemoveIntcHandler(timerCause, ids[2]);
}

// The same list on a channel of the transfer controller.
static void testDmacHandlers() {
	static const int channel = 9;
	s32 ids[2];

	ids[0] = AddDmacHandler(channel, &dmacHandler<1, 0>, -1);
	ids[1] = AddDmacHandler(channel, &dmacHandler<2, 0>, -1);
	schedf("Two transfer handlers, ids %d %d\n", ids[0], ids[1]);
	schedf("  enable %d, disable %d\n", EnableDmac(channel), DisableDmac(channel));

	for (int i = 0; i < 2; ++i) {
		if (ids[i] >= 0) {
			schedf("  remove %d\n", RemoveDmacHandler(channel, ids[i]));
		}
	}
}

// How many handlers the kernel will take on one cause.
static void testLimit() {
	static const int wanted = 40;
	static s32 ids[40];
	int made = 0;

	for (; made < wanted; ++made) {
		ids[made] = AddIntcHandler(timerCause, &intcHandler<9, 0>, -1);
		if (ids[made] < 0) {
			break;
		}
	}
	schedf("Handlers on one cause before a refusal: %d, then %d\n", made,
	       made < wanted ? ids[made] : 0);
	for (int i = 0; i < made; ++i) {
		RemoveIntcHandler(timerCause, ids[i]);
	}
}

// The alarm list, which the kernel drives from its own counter.
static s32 alarmMark;

static void alarmHandler(s32 id, u16 time, void *common) {
	alarmMark = id;
}

static void testAlarm() {
	alarmMark = -1;
	const s32 id = SetAlarm(100, &alarmHandler, 0);
	schedf("SetAlarm: id %d\n", id);
	if (id < 0) {
		return;
	}
	spin(200000);
	schedf("  handler saw %d, release %d, release again %d\n", alarmMark,
	       ReleaseAlarm(id), ReleaseAlarm(id));
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testIntcOrder();
	testReturnValue();
	testRemove();
	testDmacHandlers();
	testLimit();
	testAlarm();

	flushschedf();

	printf("-- TEST END\n");
	return 0;
}
