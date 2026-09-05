#include <common-ee.h>
#include <kernel.h>

// The scratchpad is sixteen kilobytes with no cache behind it, mapped where
// nothing else is.  Where it repeats, what a narrow access does, and whether
// the cached and uncached views of main memory reach it are the questions.

static volatile u8 *const scratch = (volatile u8 *)0x70000000;
static const u32 scratchSize = 16 * 1024;

static void fill() {
	volatile u32 *words = (volatile u32 *)scratch;
	for (u32 i = 0; i < scratchSize / 4; ++i) {
		words[i] = 0x5000 + i;
	}
}

// A marker written at one offset, and every offset it can then be read from.
static void testAliases() {
	// An access at or past the end of the scratchpad faults, so whether it
	// repeats above itself is a question this cannot ask.
	static const u32 offsets[] = {0x0000, 0x0004, 0x0010, 0x1000, 0x2000, 0x3000,
	                              0x3FF0, 0x3FFC};

	printf("Aliases:\n");
	for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
		fill();
		volatile u32 *words = (volatile u32 *)scratch;
		words[offsets[i] / 4] = 0xABCD0000 + i;

		printf("  wrote %04x, reads back at:", offsets[i]);
		for (unsigned j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j) {
			if (words[offsets[j] / 4] == 0xABCD0000 + i) {
				printf(" %04x", offsets[j]);
			}
		}
		printf("\n");
	}
}

static void testWidths() {
	printf("Access widths:\n");
	volatile u32 *words = (volatile u32 *)scratch;
	volatile u16 *halves = (volatile u16 *)scratch;
	volatile u8 *bytes = scratch;

	words[0] = 0x11223344;
	bytes[0] = 0xAA;
	const u32 afterByte = words[0];

	words[0] = 0x11223344;
	halves[0] = 0xBBCC;
	const u32 afterHalf = words[0];

	words[0] = 0x11223344;
	const u8 readByte = bytes[1];
	const u16 readHalf = halves[1];

	printf("  byte write %08x, half write %08x, byte read %02x, half read %04x\n",
	       afterByte, afterHalf, readByte, readHalf);

	static u32 __attribute__((aligned(16))) pattern[4] = {0x01234567, 0x89ABCDEF,
	                                                      0xFEDCBA98, 0x76543210};
	for (int i = 0; i < 4; ++i) {
		words[i] = 0;
	}
	*(volatile u128 *)scratch = *(vu128 *)pattern;
	printf("  quadword: %08x %08x %08x %08x\n", words[0], words[1], words[2],
	       words[3]);

	u64 wide = 0;
	asm volatile ("ld %0, 0(%1)\n" : "=r"(wide) : "r"((const void *)scratch));
	printf("  doubleword read: %016llx\n", wide);
}

// The two windows onto main memory, which the scratchpad sits outside of.
static void testMainMemoryViews() {
	static u32 __attribute__((aligned(64))) target = 0;

	printf("Cached and uncached views of one word:\n");
	const u32 address = (u32)&target;
	volatile u32 *cached = (volatile u32 *)(address & 0x0FFFFFFF);
	volatile u32 *uncached = (volatile u32 *)((address & 0x0FFFFFFF) | 0x20000000);
	volatile u32 *accelerated = (volatile u32 *)((address & 0x0FFFFFFF) | 0x30000000);

	*cached = 0x11111111;
	FlushCache(0);
	printf("  wrote through %08x: cached %08x uncached %08x accelerated %08x\n",
	       (u32)cached, *cached, *uncached, *accelerated);

	*uncached = 0x22222222;
	FlushCache(0);
	printf("  wrote through %08x: cached %08x uncached %08x accelerated %08x\n",
	       (u32)uncached, *cached, *uncached, *accelerated);
}

// Whether the scratchpad answers through the mirror bits main memory uses.
static void testScratchpadWindows() {
	static const u32 bases[] = {0x70000000, 0x70001000, 0x70003FF0};

	printf("Scratchpad windows:\n");
	fill();
	for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
		volatile u32 *at = (volatile u32 *)bases[i];
		printf("  %08x: %08x %08x\n", bases[i], at[0], at[1]);
	}
}

// A transfer into the scratchpad through its own channel, which is the way a
// program is meant to reach it.
static void testChannelAddress() {
	printf("Scratchpad address register bits:\n");
	volatile u32 *const sadr = (volatile u32 *)0x1000D080;
	static const u32 values[] = {0x00000000, 0x00003FF0, 0x00004000, 0xFFFFFFFF};

	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		*sadr = values[i];
		printf("  wrote %08x, reads %08x\n", values[i], *sadr);
	}
	*sadr = 0;
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testAliases();
	testWidths();
	testMainMemoryViews();
	testScratchpadWindows();
	testChannelAddress();

	printf("-- TEST END\n");
	return 0;
}
