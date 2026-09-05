#include <common-iop.h>
#include <intrman.h>
#include <sysclib.h>
#include <thbase.h>

// The interrupt manager: which numbers already have a handler, how the suspend
// count nests, and what the calls answer outside an interrupt.  Registering
// only ever succeeds on a number nothing is using, so nothing is displaced.

static volatile int handlerRuns;

static int countingHandler(void *argument) {
	handlerRuns++;
	return 1;
}

// Registering on each number says which the kernel has already taken.
static void testWhichAreTaken(void) {
	int number;
	int taken = 0;
	int free = 0;

	printf("Registering a handler on each number:\n");
	for (number = 0; number < 48; ++number) {
		const int result = RegisterIntrHandler(number, 1, &countingHandler, 0);
		if (result >= 0) {
			printf("  %2d: free, register %d, release %d\n", number, result,
			       ReleaseIntrHandler(number));
			free++;
		} else {
			printf("  %2d: taken, register %d\n", number, result);
			taken++;
		}
	}
	printf("  %d taken, %d free\n", taken, free);
}

// Numbers past the end, which the manager has to refuse rather than index.
static void testBadNumbers(void) {
	static const int numbers[] = {-1, 48, 63, 64, 0x100, 0x7FFFFFFF};
	unsigned i;

	printf("Numbers past the end:\n");
	for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
		int state = 0;
		printf("  %11d: register %d, release %d, enable %d, disable %d\n",
		       numbers[i], RegisterIntrHandler(numbers[i], 1, &countingHandler, 0),
		       ReleaseIntrHandler(numbers[i]), EnableIntr(numbers[i]),
		       DisableIntr(numbers[i], &state));
	}
}

// Registering twice on the same free number, and releasing one twice.
static void testDoubleRegister(void) {
	int number;

	printf("Registering twice on the same number:\n");
	for (number = 0; number < 48; ++number) {
		int first = RegisterIntrHandler(number, 1, &countingHandler, 0);
		if (first < 0) {
			continue;
		}
		printf("  number %d: first %d, second %d\n", number, first,
		       RegisterIntrHandler(number, 1, &countingHandler, 0));
		printf("  release %d, again %d\n", ReleaseIntrHandler(number),
		       ReleaseIntrHandler(number));
		break;
	}
	if (number == 48) {
		printf("  no free number to try\n");
	}
}

// The suspend count, which is documented as refusing a second suspend rather
// than counting.
static void testSuspendNesting(void) {
	int first = 0;
	int second = 0;
	int firstResult;
	int secondResult;

	printf("Suspending twice:\n");
	firstResult = CpuSuspendIntr(&first);
	secondResult = CpuSuspendIntr(&second);
	printf("  first %d state %d, second %d state %d\n", firstResult, first,
	       secondResult, second);

	printf("  resume %d\n", CpuResumeIntr(first));
	printf("  resume again %d\n", CpuResumeIntr(first));

	printf("  suspend once more %d\n", CpuSuspendIntr(&first));
	printf("  resume %d\n", CpuResumeIntr(first));
}

// The pair that turn interrupts off and on without a state to hold.
static void testEnableDisable(void) {
	int disabled;
	int enabled;

	printf("Turning interrupts off and on:\n");
	disabled = CpuDisableIntr();
	enabled = CpuEnableIntr();
	printf("  disable %d, enable %d\n", disabled, enabled);

	disabled = CpuDisableIntr();
	printf("  disable %d, disable again %d\n", disabled, CpuDisableIntr());
	printf("  enable %d, enable again %d\n", CpuEnableIntr(), CpuEnableIntr());
}

// What the manager says about where the program is running.
static void testContext(void) {
	printf("Outside any handler:\n");
	printf("  QueryIntrContext %d\n", QueryIntrContext());
}

// The enable and disable calls on a number nothing has registered, which is a
// different answer from one that is taken.
static void testEnableWithoutHandler(void) {
	int number;

	printf("Enabling a number with no handler:\n");
	for (number = 0; number < 48; ++number) {
		int state = 0;
		if (RegisterIntrHandler(number, 1, &countingHandler, 0) >= 0) {
			ReleaseIntrHandler(number);
			printf("  number %d: enable %d, disable %d\n", number,
			       EnableIntr(number), DisableIntr(number, &state));
			printf("  the state it reported was %d\n", state);
			break;
		}
	}
}

// The mode argument, which picks how the handler is dispatched.
static void testModes(void) {
	static const int modes[] = {0, 1, 2, 3, -1};
	unsigned i;
	int number;

	for (number = 0; number < 48; ++number) {
		if (RegisterIntrHandler(number, 1, &countingHandler, 0) >= 0) {
			ReleaseIntrHandler(number);
			break;
		}
	}
	if (number == 48) {
		printf("No free number for the mode test\n");
		return;
	}

	printf("Each mode on number %d:\n", number);
	for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
		const int result =
			RegisterIntrHandler(number, modes[i], &countingHandler, 0);
		printf("  mode %2d: register %d, release %d\n", modes[i], result,
		       result >= 0 ? ReleaseIntrHandler(number) : 0);
	}
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testContext();
	testSuspendNesting();
	testEnableDisable();
	testWhichAreTaken();
	testBadNumbers();
	testDoubleRegister();
	testEnableWithoutHandler();
	testModes();

	printf("-- TEST END\n");
	return 1;
}
