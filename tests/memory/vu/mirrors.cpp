#include <common-ee.h>

// Each unit gets a 16 KB window whether or not it has 16 KB behind it, so
// where a short one repeats is the question.
struct Window {
	const char *name;
	volatile u32 *base;
	u32 windowSize;
};

static const Window windowOf[] = {
	{"vu0 micro", (volatile u32 *)0x11000000, 0x4000},
	{"vu0 mem  ", (volatile u32 *)0x11004000, 0x4000},
	{"vu1 micro", (volatile u32 *)0x11008000, 0x4000},
	{"vu1 mem  ", (volatile u32 *)0x1100C000, 0x4000},
};

static const int windowCount = sizeof(windowOf) / sizeof(windowOf[0]);

static const u32 offsets[] = {0x0000, 0x0004, 0x0800, 0x0FFC, 0x1000, 0x1004,
                               0x1800, 0x2000, 0x3000, 0x3FFC};

static const int offsetCount = sizeof(offsets) / sizeof(offsets[0]);

static void clearWindow(const Window &window) {
	for (u32 at = 0; at < window.windowSize; at += 4) {
		window.base[at / 4] = 0;
	}
}

// Writes a marker at one offset and reports every offset that came back with
// it, which names the addresses that share storage.
static void findAliases(const Window &window, u32 offset) {
	clearWindow(window);
	window.base[offset / 4] = 0xA5A50000 + offset;

	printf("  %s %04x aliases:", window.name, offset);
	for (int i = 0; i < offsetCount; ++i) {
		const u32 other = offsets[i];
		if (other == offset) {
			continue;
		}
		if (window.base[other / 4] == 0xA5A50000 + offset) {
			printf(" %04x", other);
		}
	}
	printf("\n");
}

static void testAliases() {
	printf("Aliases within each window:\n");
	for (int w = 0; w < windowCount; ++w) {
		findAliases(windowOf[w], 0x0000);
		findAliases(windowOf[w], 0x0FFC);
	}
}

// The windows sit next to each other, so a write to one must not show up in
// the next.
static void testSeparation() {
	printf("Separation between windows:\n");
	for (int w = 0; w < windowCount; ++w) {
		for (int other = 0; other < windowCount; ++other) {
			windowOf[other].base[0] = 0x11110000 + other;
		}
		windowOf[w].base[0] = 0xBEEF0000 + w;

		printf("  after writing %s:", windowOf[w].name);
		for (int other = 0; other < windowCount; ++other) {
			printf(" %08x", windowOf[other].base[0]);
		}
		printf("\n");
	}
}

// Vu memory is quadword organised, so a narrow write may or may not reach it.
static void testWidths() {
	printf("Narrow access:\n");
	for (int w = 0; w < windowCount; ++w) {
		volatile u32 *base = windowOf[w].base;
		volatile u8 *bytes = (volatile u8 *)base;
		volatile u16 *halves = (volatile u16 *)base;

		base[0] = 0x11223344;
		bytes[0] = 0xAA;
		const u32 afterByte = base[0];

		base[0] = 0x11223344;
		halves[0] = 0xBBCC;
		const u32 afterHalf = base[0];

		base[0] = 0x11223344;
		const u8 readByte = bytes[1];
		const u16 readHalf = halves[1];

		printf("  %s byte write %08x, half write %08x, byte read %02x, half read %04x\n",
		       windowOf[w].name, afterByte, afterHalf, readByte, readHalf);
	}
}

// A quadword store is the access the unit is built for.
static void testQuadword() {
	static u32 __attribute__((aligned(16))) source[4] = {
		0x01234567, 0x89ABCDEF, 0xFEDCBA98, 0x76543210};

	printf("Quadword access:\n");
	for (int w = 0; w < windowCount; ++w) {
		volatile u32 *base = windowOf[w].base;
		for (int i = 0; i < 4; ++i) {
			base[i] = 0;
		}
		*(volatile u128 *)base = *(vu128 *)source;

		printf("  %s: %08x %08x %08x %08x\n", windowOf[w].name,
		       base[0], base[1], base[2], base[3]);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testAliases();
	testSeparation();
	testWidths();
	testQuadword();

	printf("-- TEST END\n");
	return 0;
}
