#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>
#include <timrman.h>

// The timer allocator: which sources and widths it hands out, what an
// allocated timer counts, and what it answers for one it never issued.  A
// timer the kernel is already using is refused, so nothing here takes one away.

static void printTimer(const char *what, int id) {
	printf("  %-30s id %3d", what, id);
	if (id >= 0) {
		printf(" counter %08x compare %08x mode %08x status %08x", GetTimerCounter(id),
		       GetTimerCompare(id), GetTimerMode(id), GetTimerStatus(id));
	}
	printf("\n");
}

// Every source and width, asked for one at a time.
static void testAllocation(void) {
	static const int sources[] = {0, 1, 2, 3, 4, 5};
	static const int sizes[] = {16, 32};
	unsigned s;
	unsigned w;

	printf("Allocating each source and width:\n");
	for (s = 0; s < sizeof(sources) / sizeof(sources[0]); ++s) {
		for (w = 0; w < sizeof(sizes) / sizeof(sizes[0]); ++w) {
			char name[40];
			const int id = AllocHardTimer(sources[s], sizes[w], 1);

			sprintf(name, "source %d, %d bits", sources[s], sizes[w]);
			printTimer(name, id);
			if (id >= 0) {
				printf("    interrupt code %d, free %d\n", GetHardTimerIntrCode(id),
				       FreeHardTimer(id));
			}
		}
	}
}

// The prescale, which decides how fast the counter moves.
static void testPrescale(void) {
	static const int prescales[] = {1, 8, 16, 256, 0, -1, 1024};
	unsigned i;

	printf("Each prescale on a system clock timer:\n");
	for (i = 0; i < sizeof(prescales) / sizeof(prescales[0]); ++i) {
		const int id = AllocHardTimer(1, 32, prescales[i]);
		char name[40];

		sprintf(name, "prescale %5d", prescales[i]);
		printTimer(name, id);
		if (id >= 0) {
			FreeHardTimer(id);
		}
	}
}

// A counter read twice, which says whether an allocated timer runs on its own.
static void testCounting(void) {
	const int id = AllocHardTimer(1, 32, 1);

	printf("A 32-bit system clock timer:\n");
	if (id < 0) {
		printf("  none available: %d\n", id);
		return;
	}

	printf("  counter %08x\n", GetTimerCounter(id));
	DelayThread(10000);
	printf("  after ten milliseconds %08x\n", GetTimerCounter(id));

	SetTimerCounter(id, 0);
	printf("  written zero, reads %08x\n", GetTimerCounter(id));
	DelayThread(10000);
	printf("  after another ten milliseconds %08x\n", GetTimerCounter(id));

	SetTimerCounter(id, 0xFFFFFF00);
	printf("  written ffffff00, reads %08x\n", GetTimerCounter(id));

	SetTimerCompare(id, 0x1000);
	printf("  compare written 1000, reads %08x, status %08x\n",
	       GetTimerCompare(id), GetTimerStatus(id));

	printf("  free %d, free again %d\n", FreeHardTimer(id), FreeHardTimer(id));
}

// The same on a 16-bit timer, whose counter has to wrap sooner.
static void testSixteenBit(void) {
	const int id = AllocHardTimer(1, 16, 1);

	printf("A 16-bit timer:\n");
	if (id < 0) {
		printf("  none available: %d\n", id);
		return;
	}

	SetTimerCounter(id, 0);
	printf("  written zero, reads %08x\n", GetTimerCounter(id));
	SetTimerCounter(id, 0xFFFFFFFF);
	printf("  written ffffffff, reads %08x\n", GetTimerCounter(id));
	SetTimerCompare(id, 0xFFFFFFFF);
	printf("  compare written ffffffff, reads %08x\n", GetTimerCompare(id));

	FreeHardTimer(id);
}

// Identifiers the allocator never handed out.
static void testBadIds(void) {
	static const int ids[] = {-1, 0, 1, 2, 0x100, 0x7FFFFFFF};
	unsigned i;

	printf("Calls with an identifier that was never issued:\n");
	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
		printf("  %11d: counter %08x compare %08x status %08x free %d\n", ids[i],
		       GetTimerCounter(ids[i]), GetTimerCompare(ids[i]),
		       GetTimerStatus(ids[i]), FreeHardTimer(ids[i]));
	}
}

// Looking one up rather than allocating it, which is how a module reaches a
// timer another one owns.
static void testRefer(void) {
	static const int sources[] = {1, 2, 3, 4};
	unsigned i;

	printf("Referring to a timer rather than allocating it:\n");
	for (i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
		const int id = ReferHardTimer(sources[i], 32, 0, 0);
		char name[40];

		sprintf(name, "source %d", sources[i]);
		printTimer(name, id);
	}
}

// How many the allocator will hand out before it runs out.
static void testLimit(void) {
	static const int wanted = 16;
	static int ids[16];
	int made = 0;
	int i;

	for (; made < wanted; ++made) {
		ids[made] = AllocHardTimer(1, 32, 1);
		if (ids[made] < 0) {
			break;
		}
	}
	printf("32-bit system clock timers before a refusal: %d, then %d\n", made,
	       made < wanted ? ids[made] : 0);
	for (i = 0; i < made; ++i) {
		FreeHardTimer(ids[i]);
	}
}

// The mode written by hand, since a program that drives a timer itself writes
// it rather than asking for one.
static void testMode(void) {
	const int id = AllocHardTimer(1, 32, 1);
	static const u32 modes[] = {0x0000, 0x0001, 0x0040, 0x0080, 0x0100, 0xFFFF};
	unsigned i;

	printf("Writing the mode:\n");
	if (id < 0) {
		printf("  no timer available: %d\n", id);
		return;
	}

	for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
		SetTimerMode(id, modes[i]);
		printf("  wrote %04x, mode reads %08x, status %08x\n", modes[i],
		       GetTimerMode(id), GetTimerStatus(id));
	}
	FreeHardTimer(id);
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testAllocation();
	testPrescale();
	testRefer();
	testCounting();
	testSixteenBit();
	testMode();
	testBadIds();
	testLimit();

	printf("-- TEST END\n");
	return 1;
}
