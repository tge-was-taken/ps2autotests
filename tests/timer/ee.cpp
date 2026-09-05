#include <common-ee.h>

// A raw count does not reproduce, so nothing here prints one: the observables
// are register readback and whether a count moved at all.

struct Timer {
	const char *name;
	volatile u32 *base;
	bool hasHold;
};

static const Timer timerOf[] = {
	{"T0", (volatile u32 *)0x10000000, true},
	{"T1", (volatile u32 *)0x10000800, true},
	{"T2", (volatile u32 *)0x10001000, false},
	{"T3", (volatile u32 *)0x10001800, false},
};

static const int timerCount = sizeof(timerOf) / sizeof(timerOf[0]);

enum ModeBits {
	MODE_CLKS_BUSCLK = 0,
	MODE_CLKS_BUSCLK16 = 1,
	MODE_CLKS_BUSCLK256 = 2,
	MODE_CLKS_HBLANK = 3,

	MODE_GATE = 1 << 2,
	MODE_GATS = 1 << 3,
	MODE_ZRET = 1 << 6,
	MODE_CUE = 1 << 7,
	MODE_CMPE = 1 << 8,
	MODE_OVFE = 1 << 9,
	MODE_EQUF = 1 << 10,
	MODE_OVFF = 1 << 11,
};

static volatile u32 *count(const Timer &timer) { return timer.base; }
static volatile u32 *mode(const Timer &timer) { return timer.base + 4; }
static volatile u32 *compare(const Timer &timer) { return timer.base + 8; }
static volatile u32 *hold(const Timer &timer) { return timer.base + 12; }

static void spin(int iterations) {
	for (volatile int i = 0; i < iterations; ++i) {
		continue;
	}
}

static void stop(const Timer &timer) {
	*mode(timer) = 0;
	*count(timer) = 0;
}

// Which mode bits stay set, and which are clear-on-write status.
static void testModeReadback() {
	printf("Mode readback:\n");
	for (int i = 0; i < timerCount; ++i) {
		const Timer &timer = timerOf[i];
		stop(timer);

		*mode(timer) = 0xFFFFFFFF;
		const u32 all = *mode(timer);
		*mode(timer) = 0x00000000;
		const u32 none = *mode(timer);
		*mode(timer) = 0x000003FF;
		const u32 low = *mode(timer);

		printf("  %s: all %08x, none %08x, low %08x\n", timer.name, all, none, low);
		stop(timer);
	}
}

// The count register is sixteen bits wide on paper.
static void testCountWidth() {
	static const u32 values[] = {0x00000000, 0x00001234, 0x0000FFFF, 0x00010000,
	                              0xFFFFFFFF};

	printf("Count readback with the timer stopped:\n");
	for (int i = 0; i < timerCount; ++i) {
		const Timer &timer = timerOf[i];
		stop(timer);

		printf("  %s:", timer.name);
		for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			*count(timer) = values[v];
			printf(" %08x", *count(timer));
		}
		printf("\n");
		stop(timer);
	}
}

static void testCompareWidth() {
	printf("Compare readback:\n");
	for (int i = 0; i < timerCount; ++i) {
		const Timer &timer = timerOf[i];
		stop(timer);

		*compare(timer) = 0xFFFFFFFF;
		const u32 all = *compare(timer);
		*compare(timer) = 0x00005678;
		const u32 some = *compare(timer);

		printf("  %s: all %08x, 5678 %08x\n", timer.name, all, some);
		stop(timer);
	}
}

// Only the first two timers have a hold register.
static void testHold() {
	printf("Hold readback:\n");
	for (int i = 0; i < timerCount; ++i) {
		const Timer &timer = timerOf[i];
		stop(timer);

		*hold(timer) = 0xFFFFFFFF;
		const u32 all = *hold(timer);
		*hold(timer) = 0x00009ABC;
		const u32 some = *hold(timer);

		printf("  %s (documented %s): all %08x, 9abc %08x\n", timer.name,
		       timer.hasHold ? "yes" : "no", all, some);
		stop(timer);
	}
}

// Whether the count moves at all, per clock source, without saying how far.
static void testCounting() {
	static const u32 sources[] = {MODE_CLKS_BUSCLK, MODE_CLKS_BUSCLK16,
	                               MODE_CLKS_BUSCLK256, MODE_CLKS_HBLANK};
	static const char *const names[] = {"busclk", "busclk/16", "busclk/256", "hblank"};

	printf("Counting:\n");
	for (int i = 0; i < timerCount; ++i) {
		const Timer &timer = timerOf[i];
		for (unsigned s = 0; s < sizeof(sources) / sizeof(sources[0]); ++s) {
			stop(timer);
			*count(timer) = 0;
			*mode(timer) = sources[s] | MODE_CUE;
			spin(20000);
			const u32 first = *count(timer);
			spin(20000);
			const u32 second = *count(timer);
			stop(timer);

			printf("  %s %-10s: started %s, kept going %s\n", timer.name, names[s],
			       first != 0 ? "yes" : "no", second != first ? "yes" : "no");
		}
	}
}

// Clearing the enable bit should leave the count where it was.
static void testEnable() {
	printf("Clearing CUE:\n");
	for (int i = 0; i < timerCount; ++i) {
		const Timer &timer = timerOf[i];
		stop(timer);
		*count(timer) = 0;
		*mode(timer) = MODE_CLKS_BUSCLK | MODE_CUE;
		spin(20000);
		*mode(timer) = MODE_CLKS_BUSCLK;
		const u32 stopped = *count(timer);
		spin(20000);
		const u32 later = *count(timer);
		stop(timer);

		printf("  %s: held %s\n", timer.name, stopped == later ? "yes" : "no");
	}
}

// ZRET is meant to send the count back to zero when it reaches the compare.
static void testZeroReturn() {
	printf("Compare with and without ZRET:\n");
	for (int i = 0; i < timerCount; ++i) {
		const Timer &timer = timerOf[i];
		for (int zret = 0; zret < 2; ++zret) {
			stop(timer);
			*count(timer) = 0;
			*compare(timer) = 0x40;
			*mode(timer) = MODE_CLKS_BUSCLK256 | MODE_CUE |
			               (zret ? MODE_ZRET : 0) | MODE_EQUF;
			spin(40000);
			const u32 reached = *count(timer);
			const u32 flags = *mode(timer);
			stop(timer);

			printf("  %s zret %d: below compare %s, EQUF %d\n", timer.name, zret,
			       reached < 0x100 ? "yes" : "no",
			       (flags & MODE_EQUF) ? 1 : 0);
		}
	}
}

// The two status bits are set by the hardware and cleared by writing a one.
static void testStatusClear() {
	printf("EQUF and OVFF clear on write:\n");
	for (int i = 0; i < timerCount; ++i) {
		const Timer &timer = timerOf[i];
		stop(timer);
		*count(timer) = 0;
		*compare(timer) = 0x20;
		*mode(timer) = MODE_CLKS_BUSCLK256 | MODE_CUE;
		spin(40000);

		const u32 raised = *mode(timer);
		*mode(timer) = raised | MODE_EQUF | MODE_OVFF;
		const u32 afterOne = *mode(timer);
		*mode(timer) = raised & ~(MODE_EQUF | MODE_OVFF);
		const u32 afterZero = *mode(timer);
		stop(timer);

		printf("  %s: raised %08x, after writing ones %08x, after writing zeroes %08x\n",
		       timer.name, raised, afterOne, afterZero);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	u32 savedMode[timerCount];
	u32 savedCompare[timerCount];
	for (int i = 0; i < timerCount; ++i) {
		savedMode[i] = *mode(timerOf[i]);
		savedCompare[i] = *compare(timerOf[i]);
	}

	testModeReadback();
	testCountWidth();
	testCompareWidth();
	testHold();
	testCounting();
	testEnable();
	testZeroReturn();
	testStatusClear();

	for (int i = 0; i < timerCount; ++i) {
		*compare(timerOf[i]) = savedCompare[i];
		*mode(timerOf[i]) = savedMode[i];
	}

	printf("-- TEST END\n");
	return 0;
}
