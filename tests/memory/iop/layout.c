#include <common-iop.h>

// How much memory this processor has and where it repeats.  A retail machine
// and a development one disagree, and the windows either side of the memory
// are what a module uses to reach hardware.

// Where a module's own buffer answers from besides its own address.  Nothing
// writes to an absolute address: the bottom of memory belongs to the kernel,
// and a marker left there takes the machine with it.
static u32 probe[4] __attribute__((aligned(16)));

static void testAliases(void) {
	static const u32 windows[] = {0x00000000, 0x80000000, 0xA0000000};
	const u32 physical = (u32)&probe[0] & 0x1FFFFFFF;
	unsigned i;

	probe[0] = 0xA5A50001;
	printf("A buffer at %08x seen through each window:\n", physical);
	for (i = 0; i < sizeof(windows) / sizeof(windows[0]); ++i) {
		volatile u32 *at = (volatile u32 *)(physical | windows[i]);
		printf("  %08x reads %08x\n", physical | windows[i], *at);
	}

	probe[0] = 0xA5A50002;
	printf("After changing it to %08x:\n", probe[0]);
	for (i = 0; i < sizeof(windows) / sizeof(windows[0]); ++i) {
		volatile u32 *at = (volatile u32 *)(physical | windows[i]);
		printf("  %08x reads %08x\n", physical | windows[i], *at);
	}
}

// The three windows the same memory answers through.
static void testWindows(void) {
	static const u32 bases[] = {0x00000000, 0x80000000, 0xA0000000};
	static u32 target;
	const u32 physical = (u32)&target & 0x1FFFFFFF;
	unsigned i;

	printf("One word through each window:\n");
	for (i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
		volatile u32 *at = (volatile u32 *)(physical | bases[i]);
		*at = 0xB0B00000 + i;
		printf("  wrote through %08x:", bases[i]);
		{
			unsigned j;
			for (j = 0; j < sizeof(bases) / sizeof(bases[0]); ++j) {
				volatile u32 *other = (volatile u32 *)(physical | bases[j]);
				printf(" %08x", *other);
			}
		}
		printf("\n");
	}
}

// The scratchpad, which sits where nothing else does.
static void testScratchpad(void) {
	volatile u32 *const scratch = (volatile u32 *)0x1F800000;
	static const u32 offsets[] = {0, 4, 0x3FC, 0x400, 0x3FF};
	unsigned i;

	printf("Scratchpad:\n");
	for (i = 0; i < 256; ++i) {
		scratch[i] = 0x5C000000 + i;
	}
	for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
		volatile u32 *at = (volatile u32 *)(0x1F800000 + (offsets[i] & ~3u));
		printf("  offset %04x: %08x\n", offsets[i], *at);
	}
}

// Narrow accesses, since a module reaching hardware uses them.
static void testWidths(void) {
	static u32 __attribute__((aligned(16))) word;
	volatile u8 *bytes = (volatile u8 *)&word;
	volatile u16 *halves = (volatile u16 *)&word;
	u32 afterByte, afterHalf;

	word = 0x11223344;
	bytes[0] = 0xAA;
	afterByte = word;

	word = 0x11223344;
	halves[0] = 0xBBCC;
	afterHalf = word;

	printf("Narrow access: byte write %08x, half write %08x\n", afterByte,
	       afterHalf);
	word = 0x11223344;
	printf("  byte reads %02x %02x %02x %02x, halves %04x %04x\n", bytes[0],
	       bytes[1], bytes[2], bytes[3], halves[0], halves[1]);
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testAliases();
	testWindows();
	testScratchpad();
	testWidths();

	printf("-- TEST END\n");
	return 0;
}
