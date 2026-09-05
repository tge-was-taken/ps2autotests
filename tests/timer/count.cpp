#include <common-ee.h>
#include <kernel.h>

// The counter inside the processor rather than the four outside it.  Nothing
// prints a count, since that would be a time: what is recorded is whether it
// moves, what a write puts back, and which bits Compare keeps.

static u32 readCount() {
	u32 value;
	asm volatile ("mfc0 %0, $9\n" "sync.p\n" : "=r"(value));
	return value;
}

static void writeCount(u32 value) {
	asm volatile ("mtc0 %0, $9\n" "sync.p\n" : : "r"(value));
}

static u32 readCompare() {
	u32 value;
	asm volatile ("mfc0 %0, $11\n" "sync.p\n" : "=r"(value));
	return value;
}

static void writeCompare(u32 value) {
	asm volatile ("mtc0 %0, $11\n" "sync.p\n" : : "r"(value));
}

static u32 readCause() {
	u32 value;
	asm volatile ("mfc0 %0, $13\n" "sync.p\n" : "=r"(value));
	return value;
}

static void spin(int iterations) {
	for (volatile int i = 0; i < iterations; ++i) {
		continue;
	}
}

static void testMoves() {
	const u32 first = readCount();
	spin(20000);
	const u32 second = readCount();
	spin(20000);
	const u32 third = readCount();

	schedf("Count moves: %s, keeps moving: %s\n", first != second ? "yes" : "no",
	       second != third ? "yes" : "no");
}

static void testWrite() {
	static const u32 values[] = {0x00000000, 0x00000001, 0x0000FFFF, 0x7FFFFFFF,
	                             0x80000000, 0xFFFFFFFF};

	schedf("Writing Count:\n");
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		writeCount(values[i]);
		const u32 back = readCount();
		schedf("  wrote %08x, read %08x, difference %08x\n", values[i], back,
		       back - values[i]);
	}
}

static void testCompareWrite() {
	static const u32 values[] = {0x00000000, 0x0000FFFF, 0xFFFFFFFF, 0x12345678};

	schedf("Writing Compare:\n");
	const u32 saved = readCompare();
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		writeCompare(values[i]);
		schedf("  wrote %08x, read %08x\n", values[i], readCompare());
	}
	writeCompare(saved);
}

// The counter interrupt shows in the cause register, and writing Compare is
// what clears it.
static void testCompareInterrupt() {
	const u32 savedCompare = readCompare();
	const u32 savedCount = readCount();

	schedf("Reaching Compare:\n");
	schedf("  cause before %08x\n", readCause());

	DIntr();
	writeCount(0);
	writeCompare(0x1000);
	spin(40000);
	const u32 duringCause = readCause();
	const u32 duringCount = readCount();
	writeCompare(savedCompare);
	const u32 afterCause = readCause();
	writeCount(savedCount);
	EIntr();

	schedf("  count passed compare: %s\n", duringCount > 0x1000 ? "yes" : "no");
	schedf("  cause while past %08x, after rewriting compare %08x\n", duringCause,
	       afterCause);
}

// Whether the counter keeps running with interrupts masked, which says it is a
// counter rather than an interrupt.
static void testWhileMasked() {
	DIntr();
	const u32 first = readCount();
	spin(20000);
	const u32 second = readCount();
	EIntr();

	schedf("Count moves with interrupts off: %s\n", first != second ? "yes" : "no");
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testMoves();
	testWrite();
	testCompareWrite();
	testCompareInterrupt();
	testWhileMasked();

	flushschedf();

	printf("-- TEST END\n");
	return 0;
}
