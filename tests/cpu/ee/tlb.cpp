#include <common-ee.h>

// The address translation entries.  Writing one that overlaps a mapping the
// kernel is using shuts the unit down, so this only ever touches an entry that
// reads back empty, uses an address range nothing else maps, and puts the
// original contents back.

struct Entry {
	u32 hi;
	u32 lo0;
	u32 lo1;
	u32 mask;
};

static Entry readEntry(int index) {
	Entry entry;
	asm volatile (
		"mtc0 %4, $0\n"
		"sync.p\n"
		"tlbr\n"
		"sync.p\n"
		"mfc0 %0, $10\n"
		"mfc0 %1, $2\n"
		"mfc0 %2, $3\n"
		"mfc0 %3, $5\n"
		: "=&r"(entry.hi), "=&r"(entry.lo0), "=&r"(entry.lo1), "=&r"(entry.mask)
		: "r"(index)
	);
	return entry;
}

static void writeEntry(int index, const Entry &entry) {
	asm volatile (
		"mtc0 %0, $0\n"
		"mtc0 %1, $10\n"
		"mtc0 %2, $2\n"
		"mtc0 %3, $3\n"
		"mtc0 %4, $5\n"
		"sync.p\n"
		"tlbwi\n"
		"sync.p\n"
		: : "r"(index), "r"(entry.hi), "r"(entry.lo0), "r"(entry.lo1),
		    "r"(entry.mask)
	);
}

static u32 probe(u32 address) {
	u32 index;
	const u32 savedHi = ({ u32 v; asm volatile ("mfc0 %0, $10\n" : "=r"(v)); v; });
	asm volatile (
		"mtc0 %1, $10\n"
		"sync.p\n"
		"tlbp\n"
		"sync.p\n"
		"mfc0 %0, $0\n"
		: "=&r"(index) : "r"(address)
	);
	asm volatile ("mtc0 %0, $10\n" "sync.p\n" : : "r"(savedHi));
	return index;
}

static int findEmptyEntry() {
	for (int index = 47; index >= 0; --index) {
		const Entry entry = readEntry(index);
		if (entry.hi == 0 && entry.lo0 == 0 && entry.lo1 == 0 && entry.mask == 0) {
			return index;
		}
	}
	return -1;
}

static void printEntry(const char *what, const Entry &entry) {
	printf("  %-26s hi %08x lo0 %08x lo1 %08x mask %08x\n", what, entry.hi,
	       entry.lo0, entry.lo1, entry.mask);
}

static void testWiredAndRandom() {
	u32 wired, random;
	asm volatile ("mfc0 %0, $6\n" "sync.p\n" : "=r"(wired));
	asm volatile ("mfc0 %0, $1\n" "sync.p\n" : "=r"(random));
	printf("Wired %08x, Random starts at %08x\n", wired, random);

	// Random counts down to Wired and wraps, so writing Wired moves its floor.
	asm volatile ("mtc0 %0, $6\n" "sync.p\n" : : "r"(0x20));
	u32 raised;
	asm volatile ("mfc0 %0, $6\n" "sync.p\n" : "=r"(raised));
	asm volatile ("mtc0 %0, $6\n" "sync.p\n" : : "r"(wired));
	printf("  Wired written 20 reads back %08x\n", raised);

	asm volatile ("mtc0 %0, $0\n" "sync.p\n" : : "r"(0xFFFFFFFF));
	u32 index;
	asm volatile ("mfc0 %0, $0\n" "sync.p\n" : "=r"(index));
	printf("  Index written ffffffff reads back %08x\n", index);
}

// The address that a bad access left behind, and the register the refill
// handler builds its pointer from.
static void testContextAndBadAddress() {
	u32 context, badAddress;
	asm volatile ("mfc0 %0, $4\n" "sync.p\n" : "=r"(context));
	asm volatile ("mfc0 %0, $8\n" "sync.p\n" : "=r"(badAddress));
	printf("Context %08x, BadVAddr %08x\n", context, badAddress);

	asm volatile ("mtc0 %0, $4\n" "sync.p\n" : : "r"(0xFFFFFFFF));
	u32 written;
	asm volatile ("mfc0 %0, $4\n" "sync.p\n" : "=r"(written));
	asm volatile ("mtc0 %0, $4\n" "sync.p\n" : : "r"(context));
	printf("  Context written ffffffff reads back %08x\n", written);
}

static void testRoundTrip(int index) {
	// A range the kernel does not map, so a valid entry here cannot collide.
	static const u32 base = 0x30000000;

	static const Entry cases[] = {
		{base | 0x00, 0x00000006, 0x00000046, 0x00000000},
		{base | 0x00, 0x00000007, 0x00000047, 0x00006000},
		{base | 0x00, 0x00000007, 0x00000047, 0x0001E000},
		{base | 0x00, 0x00000007, 0x00000047, 0x0007E000},
		{base | 0x00, 0x00000007, 0x00000047, 0x001FE000},
		{base | 0x00, 0x00000007, 0x00000047, 0x007FE000},
		{base | 0x00, 0x00000007, 0x00000047, 0x01FE0000},
		// The global bit set on only one half, which the entry keeps as one bit.
		{base | 0x00, 0x00000007, 0x00000046, 0x00000000},
		{base | 0x00, 0x00000006, 0x00000047, 0x00000000},
		// The identifier in the low byte, and every bit the format does not use.
		{base | 0xFF, 0x00000007, 0x00000047, 0x00000000},
		{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		// The scratchpad bit, which the entry carries above the frame number.
		{base | 0x00, 0x80000007, 0x80000047, 0x00000000},
	};
	static const int caseCount = sizeof(cases) / sizeof(cases[0]);

	printf("Writing entry %d and reading it back:\n", index);
	for (int i = 0; i < caseCount; ++i) {
		// The whole-ones case would map an address the kernel uses, so it is
		// written with the valid bits cleared.
		Entry attempt = cases[i];
		if (attempt.hi == 0xFFFFFFFF) {
			attempt.lo0 &= ~2u;
			attempt.lo1 &= ~2u;
		}
		writeEntry(index, attempt);
		const Entry back = readEntry(index);
		printEntry("wrote", attempt);
		printEntry("  reads", back);
	}
}

static void testProbe(int index) {
	static const u32 base = 0x30000000;
	static const u32 addresses[] = {0x30000000, 0x30000FFF, 0x30001000, 0x30002000,
	                                0x2FFFF000, 0x00100000, 0x70000000};

	const Entry mapping = {base, 0x00000007, 0x00000047, 0x00000000};
	writeEntry(index, mapping);

	printf("Probing around a mapping at %08x:\n", base);
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		const u32 result = probe(addresses[i]);
		printf("  %08x: %08x, %s\n", addresses[i], result,
		       (result & 0x80000000) ? "no match" : "matched");
	}

	// The same probe with the largest page, which widens what one entry covers.
	const Entry wide = {base, 0x00000007, 0x00000047, 0x01FE0000};
	writeEntry(index, wide);
	printf("Probing the same addresses with the largest page:\n");
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		const u32 result = probe(addresses[i]);
		printf("  %08x: %08x, %s\n", addresses[i], result,
		       (result & 0x80000000) ? "no match" : "matched");
	}
}

// Which entries the kernel left in place, so a capture says what a running
// program starts with.
static void testExistingEntries() {
	printf("Entries the kernel installed:\n");
	int used = 0;
	for (int index = 0; index < 48; ++index) {
		const Entry entry = readEntry(index);
		if (entry.hi == 0 && entry.lo0 == 0 && entry.lo1 == 0 && entry.mask == 0) {
			continue;
		}
		printf("  %2d: hi %08x lo0 %08x lo1 %08x mask %08x\n", index, entry.hi,
		       entry.lo0, entry.lo1, entry.mask);
		used++;
	}
	printf("  %d of 48 in use\n", used);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testWiredAndRandom();
	testContextAndBadAddress();
	testExistingEntries();

	const int spare = findEmptyEntry();
	printf("The highest empty entry is %d\n", spare);
	if (spare >= 0) {
		const Entry original = readEntry(spare);
		testRoundTrip(spare);
		testProbe(spare);
		writeEntry(spare, original);
		printEntry("put back", readEntry(spare));
	}

	printf("-- TEST END\n");
	return 0;
}
