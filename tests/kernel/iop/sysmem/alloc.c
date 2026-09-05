#include <common-iop.h>
#include <sysclib.h>
#include <sysmem.h>

// Where the allocator puts a block, how it rounds a size, and what it reports
// about the memory it has left.  An emulated kernel that gets the addresses
// wrong breaks every module that keeps a pointer.

static void printSizes(const char *what) {
	printf("  %-22s total %8d, max free %8d, free %8d\n", what, QueryMemSize(),
	       QueryMaxFreeMemSize(), QueryTotalFreeMemSize());
}

// The three placements, each asked for the same size.
static void testPlacement(void) {
	static const int sizes[] = {16, 64, 256, 4096, 0x10000};
	unsigned i;

	printf("Allocating from each end:\n");
	printSizes("before");

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		void *low = AllocSysMemory(ALLOC_FIRST, sizes[i], 0);
		void *high = AllocSysMemory(ALLOC_LAST, sizes[i], 0);

		printf("  %6d bytes: low %08x high %08x, blocks %d and %d\n", sizes[i],
		       (u32)low, (u32)high,
		       low != 0 ? QueryBlockSize(low) : -1,
		       high != 0 ? QueryBlockSize(high) : -1);

		if (low != 0) {
			FreeSysMemory(low);
		}
		if (high != 0) {
			FreeSysMemory(high);
		}
	}
	printSizes("after freeing");
}

// A size the allocator has to round, and the block it reports back.
static void testRounding(void) {
	static const int sizes[] = {1, 2, 15, 16, 17, 63, 64, 65, 255, 256, 257};
	unsigned i;

	printf("Rounding:\n");
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		void *block = AllocSysMemory(ALLOC_FIRST, sizes[i], 0);
		if (block == 0) {
			printf("  %4d: refused\n", sizes[i]);
			continue;
		}
		printf("  %4d: at %08x, size %d, top %08x\n", sizes[i], (u32)block,
		       QueryBlockSize(block), (u32)QueryBlockTopAddress(block));
		FreeSysMemory(block);
	}
}

// Two blocks in a row, so the gap between them says how the allocator packs.
static void testAdjacent(void) {
	void *first = AllocSysMemory(ALLOC_FIRST, 64, 0);
	void *second = AllocSysMemory(ALLOC_FIRST, 64, 0);
	void *third = AllocSysMemory(ALLOC_FIRST, 64, 0);

	printf("Three blocks of 64 from the low end:\n");
	printf("  %08x %08x %08x\n", (u32)first, (u32)second, (u32)third);
	if (first != 0 && second != 0) {
		printf("  gap %d\n", (int)((u32)second - (u32)first));
	}

	if (second != 0) {
		FreeSysMemory(second);
	}
	void *replacement = AllocSysMemory(ALLOC_FIRST, 64, 0);
	printf("  after freeing the middle, a new block lands at %08x\n",
	       (u32)replacement);

	if (first != 0) {
		FreeSysMemory(first);
	}
	if (third != 0) {
		FreeSysMemory(third);
	}
	if (replacement != 0) {
		FreeSysMemory(replacement);
	}
}

// Asking for a particular address, which is the third placement.
static void testAtAddress(void) {
	void *anchor = AllocSysMemory(ALLOC_FIRST, 4096, 0);

	printf("Allocating at an address:\n");
	if (anchor == 0) {
		printf("  no anchor\n");
		return;
	}

	void *inside = AllocSysMemory(ALLOC_FIRST | 2, 64, anchor);
	void *after = AllocSysMemory(ALLOC_FIRST | 2, 64, (void *)((u32)anchor + 4096));
	printf("  inside the anchor %08x, just after it %08x\n", (u32)inside, (u32)after);

	if (inside != 0) {
		FreeSysMemory(inside);
	}
	if (after != 0) {
		FreeSysMemory(after);
	}
	FreeSysMemory(anchor);
}

// Sizes the allocator cannot possibly satisfy.
static void testRefusals(void) {
	static const int sizes[] = {0, -1, 0x7FFFFFFF};
	unsigned i;

	printf("Sizes that should be refused:\n");
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		void *block = AllocSysMemory(ALLOC_FIRST, sizes[i], 0);
		printf("  %11d: %08x\n", sizes[i], (u32)block);
		if (block != 0) {
			FreeSysMemory(block);
		}
	}

	printf("Freeing something never allocated:\n");
	printf("  null %d\n", FreeSysMemory(0));
	printf("  a stack address %d\n", FreeSysMemory((void *)&i));
}

// The largest single block the allocator will hand over, and whether taking it
// leaves anything behind.
static void testLargest(void) {
	const int largest = QueryMaxFreeMemSize();
	void *block = AllocSysMemory(ALLOC_FIRST, largest, 0);

	printf("The largest free block:\n");
	printf("  asked %d, got %08x\n", largest, (u32)block);
	printSizes("while held");
	if (block != 0) {
		FreeSysMemory(block);
	}
	printSizes("after freeing");
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	printSizes("at entry");
	testPlacement();
	testRounding();
	testAdjacent();
	testAtAddress();
	testRefusals();
	testLargest();

	printf("-- TEST END\n");
	return 0;
}
