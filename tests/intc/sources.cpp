#include <common-ee.h>
#include <ee_regs.h>
#include <kernel.h>

// Which of the fifteen interrupt sources a program can raise from its own
// side, and what the status register shows for each.  Masking a source the
// kernel needs would take the console with it, so the output is buffered.

static volatile u32 *const intcStat = (volatile u32 *)0x1000F000;
static volatile u32 *const intcMask = (volatile u32 *)0x1000F010;

struct Source {
	int bit;
	const char *name;
};

static const Source sources[] = {
	{0,  "GS"},     {1,  "SBUS"},   {2,  "VBON"},   {3,  "VBOF"},
	{4,  "VIF0"},   {5,  "VIF1"},   {6,  "VU0"},    {7,  "VU1"},
	{8,  "IPU"},    {9,  "TIMER0"}, {10, "TIMER1"}, {11, "TIMER2"},
	{12, "TIMER3"}, {13, "SFIFO"},  {14, "VU0WD"},  {15, "unused15"},
};

static const int sourceCount = sizeof(sources) / sizeof(sources[0]);

// Whatever the loader left in the toggle register, writing it back clears it.
static void clearMask() {
	*intcMask = *intcMask;
}

// Each bit set on its own, so the mask register is shown to hold every one.
static void testMaskBits() {
	schedf("Setting each mask bit alone:\n");
	clearMask();
	for (int i = 0; i < sourceCount; ++i) {
		*intcMask = 1u << sources[i].bit;
		schedf("  %-8s bit %2d: mask %08x\n", sources[i].name, sources[i].bit,
		       *intcMask);
		*intcMask = 1u << sources[i].bit;
	}
	schedf("  after clearing each again: mask %08x\n", *intcMask);
}

// A source with nothing driving it should read clear, and writing a one to it
// should leave it clear.
static void testStatusBits() {
	schedf("Status with nothing running:\n");
	*intcStat = 0xFFFFFFFF;
	schedf("  after clearing everything: stat %08x\n", *intcStat);

	for (int i = 0; i < sourceCount; ++i) {
		*intcStat = 1u << sources[i].bit;
		schedf("  wrote %-8s: stat %08x\n", sources[i].name, *intcStat);
	}
}

// The two timer sources a program can raise on its own, which is the only way
// to see the status bit go up without another unit running.
static void testTimerSources() {
	static const u32 timerBases[] = {0x10000000, 0x10000800, 0x10001000, 0x10001800};
	static const int timerBits[] = {9, 10, 11, 12};

	schedf("Raising a timer interrupt:\n");
	clearMask();
	*intcStat = 0xFFFFFFFF;

	for (int i = 0; i < 4; ++i) {
		volatile u32 *count = (volatile u32 *)timerBases[i];
		volatile u32 *mode = (volatile u32 *)(timerBases[i] + 0x10);
		volatile u32 *compare = (volatile u32 *)(timerBases[i] + 0x20);

		const u32 savedMode = *mode;
		*mode = 0;
		*count = 0;
		*compare = 0x20;
		// Clock over 256, compare interrupt enabled, both status bits cleared.
		*mode = 2 | (1 << 7) | (1 << 8) | (1 << 10) | (1 << 11);
		for (volatile int spin = 0; spin < 40000; ++spin) {
			continue;
		}
		const u32 stat = *intcStat;
		const u32 modeAfter = *mode;
		*mode = 0;
		*intcStat = 1u << timerBits[i];
		const u32 cleared = *intcStat;
		*mode = savedMode;

		schedf("  TIMER%d: stat %08x bit %d %s, mode %08x, after clearing %08x\n", i,
		       stat, timerBits[i], (stat >> timerBits[i]) & 1 ? "set" : "clear",
		       modeAfter, cleared);
	}
}

// The vertical blank sources tick on their own, so this says whether they are
// running at all without waiting a fixed time for one.
static void testVerticalBlank() {
	schedf("Vertical blank sources:\n");
	*intcStat = 0xFFFFFFFF;

	int sawOn = 0;
	int sawOff = 0;
	for (int i = 0; i < 5000000; ++i) {
		const u32 stat = *intcStat;
		if (stat & (1 << 2)) {
			sawOn = 1;
		}
		if (stat & (1 << 3)) {
			sawOff = 1;
		}
		if (sawOn && sawOff) {
			break;
		}
	}
	schedf("  VBON seen %s, VBOF seen %s\n", sawOn ? "yes" : "no",
	       sawOff ? "yes" : "no");
	*intcStat = 0xFFFFFFFF;
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	const u32 savedMask = *intcMask;

	DIntr();
	testMaskBits();
	testStatusBits();
	testTimerSources();
	EIntr();
	testVerticalBlank();

	*intcMask = *intcMask ^ savedMask;

	flushschedf();

	printf("-- TEST END\n");
	return 0;
}
