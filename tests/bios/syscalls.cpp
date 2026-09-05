#include <common-ee.h>
#include <kernel.h>
#include <string.h>

// The calls that reach into the kernel itself: replacing an entry in its table,
// flushing the caches, and the entries that manage the address translation.

// SetSyscall is not tested here.  A number the kernel already uses is
// overwritten with no way to read the old entry back and put it there, and the
// numbers it does not use are what the test would have to know in advance.
// The cache flush, whose argument picks which of the two to write back.
static void testFlushCache() {
	static u32 __attribute__((aligned(64))) buffer[64];
	static const s32 operations[] = {0, 1, 2};

	printf("FlushCache:\n");
	for (unsigned i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i) {
		volatile u32 *const cached = buffer;
		volatile u32 *const uncached =
			(volatile u32 *)(((u32)buffer & 0x1FFFFFFF) | 0xA0000000);

		uncached[0] = 0xAAAA0000 + i;
		cached[0] = 0xBBBB0000 + i;
		FlushCache(operations[i]);
		printf("  operation %d: uncached reads %08x\n", operations[i], uncached[0]);
	}

	printf("  iFlushCache from a thread: ");
	iFlushCache(0);
	printf("returned\n");
}

// The translation entries the kernel installed, read straight out of the unit.
static void testTranslation() {
	printf("Translation entries:\n");
	for (int index = 0; index < 48; ++index) {
		u32 entryHi, entryLo0, entryLo1, pageMask;
		asm volatile (
			"mtc0 %4, $0\n"
			"sync.p\n"
			"tlbr\n"
			"sync.p\n"
			"mfc0 %0, $10\n"
			"mfc0 %1, $2\n"
			"mfc0 %2, $3\n"
			"mfc0 %3, $5\n"
			: "=&r"(entryHi), "=&r"(entryLo0), "=&r"(entryLo1), "=&r"(pageMask)
			: "r"(index)
		);
		if (entryHi == 0 && entryLo0 == 0 && entryLo1 == 0 && pageMask == 0) {
			continue;
		}
		printf("  %2d: hi %08x lo0 %08x lo1 %08x mask %08x\n", index, entryHi,
		       entryLo0, entryLo1, pageMask);
	}
}

// Looking up an address the kernel has mapped, and one it has not.
static void testProbe() {
	static const u32 addresses[] = {0x00100000, 0x00200000, 0x01000000, 0x20000000,
	                                0x70000000, 0x80000000};

	printf("Probing an address:\n");
	const u32 savedHi = ({ u32 v; asm volatile ("mfc0 %0, $10\n" : "=r"(v)); v; });
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		u32 index;
		asm volatile (
			"mtc0 %1, $10\n"
			"sync.p\n"
			"tlbp\n"
			"sync.p\n"
			"mfc0 %0, $0\n"
			: "=r"(index) : "r"(addresses[i])
		);
		printf("  %08x: index %08x, %s\n", addresses[i], index,
		       (index & 0x80000000) ? "not mapped" : "mapped");
	}
	asm volatile ("mtc0 %0, $10\n" "sync.p\n" : : "r"(savedHi));
}

// The two registers that say how many entries are reserved and which one the
// hardware will replace next.
static void testWiredAndRandom() {
	u32 wired, first, second;
	asm volatile ("mfc0 %0, $6\n" "sync.p\n" : "=r"(wired));
	asm volatile ("mfc0 %0, $1\n" "sync.p\n" : "=r"(first));
	for (volatile int i = 0; i < 1000; ++i) {
		continue;
	}
	asm volatile ("mfc0 %0, $1\n" "sync.p\n" : "=r"(second));

	printf("Wired %08x, Random %08x then %08x, moved %s\n", wired, first, second,
	       first != second ? "yes" : "no");
}

// The graphics registers the kernel keeps a copy of.
static void testGraphicsRegisters() {
	const u64 mask = GsGetIMR();
	printf("GsGetIMR %016llx\n", mask);
	GsPutIMR(mask);
	printf("  put back, reads %016llx\n", GsGetIMR());

	GsPutIMR(0x0000FF00ull);
	printf("  wrote 0000ff00, reads %016llx\n", GsGetIMR());
	GsPutIMR(mask);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testFlushCache();
	testWiredAndRandom();
	testTranslation();
	testProbe();
	testGraphicsRegisters();


	printf("-- TEST END\n");
	return 0;
}
