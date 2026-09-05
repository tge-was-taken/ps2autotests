#include <common-ee.h>
#include <kernel.h>

static volatile u32 *const intcStat = (volatile u32 *)0x1000F000;
static volatile u32 *const intcMask = (volatile u32 *)0x1000F010;
static volatile u32 *const dmacStat = (volatile u32 *)0x1000E010;

// Masking an interrupt the kernel needs would take the console with it, so the
// output is buffered and only flushed once the registers are back.
static void record(const char *what, u32 wrote, u32 before, u32 after) {
	schedf("  %s: wrote %08x to %08x -> %08x\n", what, wrote, before, after);
}

// Whatever the loader left in a toggle register, writing it back clears it,
// which is what makes the rest of the test read the same on any boot path.
static void zeroToggle(volatile u32 *reg, u32 half) {
	*reg = *reg & half;
}

// The mask half of these registers is documented as toggling rather than
// assigning, which is the thing to pin down.
static void probe(volatile u32 *reg, const char *name, u32 value) {
	const u32 before = *reg;
	*reg = value;
	const u32 after = *reg;
	record(name, value, before, after);
}

static void testIntcMask() {
	static const u32 values[] = {
		0x00000000, 0x00000001, 0x00000001, 0x00000003,
		0x0000FFFF, 0x0000FFFF, 0xFFFFFFFF,
	};

	schedf("INTC_MASK, writing in sequence:\n");
	const u32 saved = *intcMask;
	zeroToggle(intcMask, 0xFFFFFFFF);
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		probe(intcMask, "mask", values[i]);
	}
	// A toggle register is put back by writing the difference.
	*intcMask = *intcMask ^ saved;
	schedf("  restored: %s\n", *intcMask == saved ? "yes" : "no");
}

static void testIntcStat() {
	static const u32 values[] = {0x00000000, 0x00000001, 0x0000FFFF, 0xFFFFFFFF};

	schedf("INTC_STAT, writing in sequence:\n");
	*intcStat = 0xFFFFFFFF;
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		probe(intcStat, "stat", values[i]);
	}
}

// The dmac packs both halves into one register: status low, mask high.
static void testDmacStat() {
	static const u32 values[] = {
		0x00000000, 0x00010000, 0x00010000, 0x00030000,
		0x00000001, 0x0000FFFF, 0xFFFF0000,
	};

	schedf("D_STAT, writing in sequence:\n");
	const u32 saved = *dmacStat;
	zeroToggle(dmacStat, 0xFFFF0000);
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		probe(dmacStat, "d_stat", values[i]);
	}
	*dmacStat = (*dmacStat ^ saved) & 0xFFFF0000;
	schedf("  restored: %s\n",
	       (*dmacStat & 0xFFFF0000) == (saved & 0xFFFF0000) ? "yes" : "no");
}

// Writing zero should be the one value that never changes anything.
static void testZeroIsInert() {
	zeroToggle(intcMask, 0xFFFFFFFF);
	zeroToggle(dmacStat, 0xFFFF0000);
	*intcStat = 0xFFFFFFFF;
	schedf("Writing zero:\n");
	const u32 mask = *intcMask;
	*intcMask = 0;
	schedf("  INTC_MASK %08x -> %08x\n", mask, *intcMask);

	const u32 stat = *intcStat;
	*intcStat = 0;
	schedf("  INTC_STAT %08x -> %08x\n", stat, *intcStat);

	const u32 dmac = *dmacStat;
	*dmacStat = 0;
	schedf("  D_STAT    %08x -> %08x\n", dmac, *dmacStat);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	const u32 savedMask = *intcMask;
	const u32 savedDmac = *dmacStat;

	DIntr();
	testIntcMask();
	testIntcStat();
	testDmacStat();
	testZeroIsInert();

	*intcMask = *intcMask ^ savedMask;
	*dmacStat = (*dmacStat ^ savedDmac) & 0xFFFF0000;
	EIntr();

	flushschedf();

	printf("-- TEST END\n");
	return 0;
}
