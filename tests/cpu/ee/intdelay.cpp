#include "shared.h"

// The two instructions that turn interrupts on and off, and how many
// instructions pass before the change takes.  The output is buffered, since
// the console needs interrupts and the register is only put back at the end.

static u32 readStatus() {
	u32 value;
	asm volatile ("mfc0 %0, $12\n" "sync.p\n" : "=r"(value));
	return value;
}

// Reading the register at each distance behind the instruction that changed
// it, which is a value rather than a time.
#define GAP_FUNC(NAME, OP, GAP) \
static u32 NAME() { \
	u32 value; \
	asm volatile ( \
		".set noreorder\n" \
		".set nomacro\n" \
		OP "\n" \
		GAP \
		"mfc0 %0, $12\n" \
		".set macro\n" \
		".set reorder\n" \
		: "=r"(value) \
	); \
	return value; \
}

GAP_FUNC(diGap0, "di", "")
GAP_FUNC(diGap1, "di", "nop\n")
GAP_FUNC(diGap2, "di", "nop\nnop\n")
GAP_FUNC(diGap4, "di", "nop\nnop\nnop\nnop\n")
GAP_FUNC(diGapSync, "di", "sync.p\n")
GAP_FUNC(eiGap0, "ei", "")
GAP_FUNC(eiGap1, "ei", "nop\n")
GAP_FUNC(eiGap2, "ei", "nop\nnop\n")
GAP_FUNC(eiGap4, "ei", "nop\nnop\nnop\nnop\n")
GAP_FUNC(eiGapSync, "ei", "sync.p\n")

typedef u32 (*GapFunction)();

static void testDisableGaps() {
	static const GapFunction runs[] = {&diGap0, &diGap1, &diGap2, &diGap4, &diGapSync};
	static const char *const names[] = {"none", "1 nop", "2 nop", "4 nop", "sync.p"};

	schedf("Reading Status after di:\n");
	for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
		asm volatile ("ei\n" "sync.p\n");
		const u32 value = runs[i]();
		schedf("  gap %-6s: %08x, EIE %d\n", names[i], value, (value >> 16) & 1);
	}
	asm volatile ("ei\n" "sync.p\n");
}

static void testEnableGaps() {
	static const GapFunction runs[] = {&eiGap0, &eiGap1, &eiGap2, &eiGap4, &eiGapSync};
	static const char *const names[] = {"none", "1 nop", "2 nop", "4 nop", "sync.p"};

	schedf("Reading Status after ei:\n");
	for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
		asm volatile ("di\n" "sync.p\n");
		const u32 value = runs[i]();
		schedf("  gap %-6s: %08x, EIE %d\n", names[i], value, (value >> 16) & 1);
	}
	asm volatile ("ei\n" "sync.p\n");
}

// The two ways to turn interrupts off, which use different bits.
static void testBits() {
	schedf("The bits each way changes:\n");

	asm volatile ("ei\n" "sync.p\n");
	const u32 enabled = readStatus();
	asm volatile ("di\n" "sync.p\n");
	const u32 disabled = readStatus();
	asm volatile ("ei\n" "sync.p\n");
	const u32 again = readStatus();

	schedf("  after ei %08x, after di %08x, after ei %08x\n", enabled, disabled,
	       again);
	schedf("  changed by di: %08x\n", enabled ^ disabled);
}

// Writing the register directly rather than through the two instructions.
static void testDirectWrite() {
	static const u32 masks[] = {1u << 16, 1u << 17, (1u << 16) | (1u << 17), 0};

	schedf("Writing the enable bits with mtc0:\n");
	const u32 saved = readStatus();
	for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
		const u32 wanted = (saved & ~0x00030000u) | masks[i];
		asm volatile ("mtc0 %0, $12\n" "sync.p\n" : : "r"(wanted));
		schedf("  wrote %08x, read %08x\n", wanted, readStatus());
	}
	asm volatile ("mtc0 %0, $12\n" "sync.p\n" : : "r"(saved));
	asm volatile ("ei\n" "sync.p\n");
}

// Whether an interrupt that arrives while they are off is still waiting when
// they come back on.
static void testPending() {
	volatile u32 *const intcStat = (volatile u32 *)0x1000F000;
	volatile u32 *const timerMode = (volatile u32 *)0x10001810;
	volatile u32 *const timerCount = (volatile u32 *)0x10001800;
	volatile u32 *const timerCompare = (volatile u32 *)0x10001820;

	schedf("An interrupt raised with interrupts off:\n");
	const u32 savedMode = *timerMode;

	asm volatile ("di\n" "sync.p\n");
	*intcStat = 0xFFFFFFFF;
	*timerMode = 0;
	*timerCount = 0;
	*timerCompare = 0x20;
	*timerMode = 2 | (1 << 7) | (1 << 8);
	for (volatile int i = 0; i < 40000; ++i) {
		continue;
	}
	const u32 whileOff = *intcStat;
	asm volatile ("ei\n" "sync.p\n");
	const u32 afterOn = *intcStat;
	*timerMode = 0;
	*intcStat = 0xFFFFFFFF;
	*timerMode = savedMode;

	schedf("  stat while off %08x, stat after on %08x\n", whileOff, afterOn);
}

// The instruction that returns from an exception, run where there is nothing
// to return from.  Its effect on the two level bits is the observable.
static void testReturnBits() {
	schedf("Status bits around the exception levels:\n");
	const u32 status = readStatus();
	schedf("  EXL %d ERL %d, both should be clear in a program\n",
	       (status >> 1) & 1, (status >> 2) & 1);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testDisableGaps();
	testEnableGaps();
	testBits();
	testDirectWrite();
	testPending();
	testReturnBits();

	flushschedf();

	printf("-- TEST END\n");
	return 0;
}
