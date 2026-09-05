#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>

// The alarm list: what the kernel accepts, when the handler runs, and what a
// handler's return value asks for.  An alarm the kernel forgets is a class of
// bug a game only meets much later.

static volatile int handlerRuns;
static volatile u32 handlerArgument;
static volatile int repeatCount;

static unsigned int oneShot(void *argument) {
	handlerRuns++;
	handlerArgument = (u32)argument;
	return 0;
}

static unsigned int repeating(void *argument) {
	repeatCount++;
	// A handler that returns a delay is documented as being called again.
	return repeatCount < 4 ? 5000 : 0;
}

static void setMicroseconds(iop_sys_clock_t *clock, u32 microseconds) {
	USec2SysClock(microseconds, clock);
}

// The delay each alarm is asked for, against how long the test waited.
static void testEachDelay(void) {
	static const u32 delays[] = {0, 1, 100, 1000, 10000, 100000};
	unsigned i;

	printf("An alarm at each delay, waited on for 50 milliseconds:\n");
	for (i = 0; i < sizeof(delays) / sizeof(delays[0]); ++i) {
		iop_sys_clock_t clock;
		int result;

		handlerRuns = 0;
		handlerArgument = 0;
		setMicroseconds(&clock, delays[i]);
		result = SetAlarm(&clock, &oneShot, (void *)(0x1000 + i));
		DelayThread(50000);

		printf("  %6d us: set %d, ran %d times, argument %08x, cancel %d\n",
		       delays[i], result, handlerRuns, handlerArgument,
		       CancelAlarm(&oneShot, (void *)(0x1000 + i)));
	}
}

// The clock value the kernel measures a delay in, and the conversion either
// way.
static void testClockConversion(void) {
	static const u32 values[] = {0, 1, 100, 1000, 1000000, 0x7FFFFFFF};
	unsigned i;

	printf("Converting between microseconds and the clock:\n");
	for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		iop_sys_clock_t clock;
		u32 seconds = 0;
		u32 microseconds = 0;

		USec2SysClock(values[i], &clock);
		SysClock2USec(&clock, &seconds, &microseconds);
		printf("  %10u us: clock %08x%08x, back as %u s %u us\n", values[i],
		       clock.hi, clock.lo, seconds, microseconds);
	}
}

// The system clock, read twice, which says it moves and roughly how fast.
static void testSystemTime(void) {
	iop_sys_clock_t first;
	iop_sys_clock_t second;

	printf("The system clock:\n");
	GetSystemTime(&first);
	DelayThread(10000);
	GetSystemTime(&second);

	printf("  %08x%08x then %08x%08x, moved %s\n", first.hi, first.lo, second.hi,
	       second.lo,
	       (second.hi != first.hi || second.lo != first.lo) ? "yes" : "no");
	printf("  ten milliseconds is about %u ticks\n", second.lo - first.lo);
}

// Cancelling an alarm that was never set, and cancelling one twice.
static void testCancel(void) {
	iop_sys_clock_t clock;
	int first;
	int second;

	printf("Cancelling:\n");
	printf("  one that was never set: %d\n", CancelAlarm(&oneShot, (void *)0x99));

	setMicroseconds(&clock, 1000000);
	handlerRuns = 0;
	SetAlarm(&clock, &oneShot, (void *)0x1234);
	first = CancelAlarm(&oneShot, (void *)0x1234);
	second = CancelAlarm(&oneShot, (void *)0x1234);
	printf("  one that was set: %d, then %d, ran %d times\n", first, second,
	       handlerRuns);

	// The argument is part of what names an alarm, so cancelling with the
	// wrong one should not find it.
	setMicroseconds(&clock, 1000000);
	SetAlarm(&clock, &oneShot, (void *)0x1234);
	printf("  with the wrong argument: %d, with the right one: %d\n",
	       CancelAlarm(&oneShot, (void *)0x5678),
	       CancelAlarm(&oneShot, (void *)0x1234));
}

// The same handler set twice, which is either two alarms or one.
static void testDuplicate(void) {
	iop_sys_clock_t clock;
	int first;
	int second;

	printf("The same handler and argument set twice:\n");

	handlerRuns = 0;
	setMicroseconds(&clock, 20000);
	first = SetAlarm(&clock, &oneShot, (void *)0x2222);
	second = SetAlarm(&clock, &oneShot, (void *)0x2222);
	DelayThread(60000);
	printf("  set %d then %d, ran %d times\n", first, second, handlerRuns);
	printf("  cancel %d, again %d\n", CancelAlarm(&oneShot, (void *)0x2222),
	       CancelAlarm(&oneShot, (void *)0x2222));
}

// A handler that returns a delay rather than zero, which asks for another run.
static void testRepeating(void) {
	iop_sys_clock_t clock;

	printf("A handler that returns a delay:\n");

	repeatCount = 0;
	setMicroseconds(&clock, 5000);
	SetAlarm(&clock, &repeating, 0);
	DelayThread(200000);
	printf("  ran %d times, cancel %d\n", repeatCount,
	       CancelAlarm(&repeating, 0));
}

// How many alarms the kernel will hold at once.
static void testLimit(void) {
	static const int wanted = 80;
	iop_sys_clock_t clock;
	int made = 0;
	int i;

	setMicroseconds(&clock, 10000000);
	for (; made < wanted; ++made) {
		if (SetAlarm(&clock, &oneShot, (void *)(0x8000 + made)) < 0) {
			break;
		}
	}
	printf("Alarms before a refusal: %d\n", made);
	for (i = 0; i < made; ++i) {
		CancelAlarm(&oneShot, (void *)(0x8000 + i));
	}
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testClockConversion();
	testSystemTime();
	testEachDelay();
	testCancel();
	testDuplicate();
	testRepeating();
	testLimit();

	printf("-- TEST END\n");
	return 1;
}
