#include <common-ee.h>
#include <kernel.h>
#include <string.h>

// The windows the same memory can be reached through.  Everything here is
// derived from one buffer this program owns, so a window that turns out not to
// alias writes somewhere harmless rather than over the kernel.

static u32 buffer[64] __attribute__((aligned(64)));

static u32 physical(const void *address) {
	return (u32)address & 0x1FFFFFFF;
}

static volatile u32 *window(u32 base, const void *address) {
	return (volatile u32 *)(base | physical(address));
}

struct Window {
	const char *name;
	u32 base;
};

static const Window windows[] = {
	{"cached, translated  ", 0x00000000},
	{"cached, untranslated", 0x80000000},
	{"uncached            ", 0xA0000000},
	{"uncached accelerated", 0x30000000},
};
static const int windowCount = sizeof(windows) / sizeof(windows[0]);

// A write through one window read back through all of them.
static void testAliasing() {
	printf("A write through each window read back through all of them:\n");

	for (int written = 0; written < windowCount; ++written) {
		const u32 value = 0xA11A0000 + written;
		volatile u32 *const target = window(windows[written].base, buffer);
		target[0] = value;
		FlushCache(0);

		printf("  wrote %08x through %s:", value, windows[written].name);
		for (int read = 0; read < windowCount; ++read) {
			printf(" %08x", window(windows[read].base, buffer)[0]);
		}
		printf("\n");
	}
}

// The address each window puts on the bus, which is the low bits alone.
static void testAddresses() {
	printf("The address of the same word in each window:\n");
	for (int i = 0; i < windowCount; ++i) {
		printf("  %s %08x\n", windows[i].name,
		       (u32)window(windows[i].base, buffer));
	}
	printf("  the buffer sits at %08x, physically %08x\n", (u32)buffer,
	       physical(buffer));
}

// Whether a console with 32 megabytes repeats its memory above that.
static void testWrap() {
	printf("Reading the same word above the end of memory:\n");

	volatile u32 *const base = window(0xA0000000, buffer);
	base[0] = 0x5A5A1234;
	FlushCache(0);

	static const u32 offsets[] = {0x02000000, 0x04000000, 0x08000000, 0x10000000};
	for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
		volatile u32 *const at = (volatile u32 *)((u32)base + offsets[i]);
		const u32 read = at[0];
		printf("  %08x reads %08x, %s\n", (u32)at, read,
		       read == 0x5A5A1234 ? "the same word" : "something else");
	}
}

// The cached and uncached views of a word the program changes through one of
// them, which is what a missing flush costs.
static void testStaleCache() {
	printf("A word changed through one view and read through the other:\n");

	volatile u32 *const cached = window(0x00000000, buffer) + 1;
	volatile u32 *const uncached = window(0xA0000000, buffer) + 1;

	cached[0] = 0x11110000;
	printf("  wrote through the cached view: cached %08x uncached %08x\n",
	       cached[0], uncached[0]);

	FlushCache(0);
	printf("  after a flush: cached %08x uncached %08x\n", cached[0], uncached[0]);

	uncached[0] = 0x22220000;
	printf("  wrote through the uncached view: cached %08x uncached %08x\n",
	       cached[0], uncached[0]);

	FlushCache(0);
	printf("  after a flush: cached %08x uncached %08x\n", cached[0], uncached[0]);
}

// The scratchpad, which is its own memory rather than a window onto anything.
static void testScratchpad() {
	printf("The scratchpad:\n");

	volatile u32 *const scratch = (volatile u32 *)0x70000000;
	scratch[0] = 0x5C5C0000;
	scratch[1] = 0x5C5C0001;

	static const u32 offsets[] = {0x0000, 0x0004, 0x1000, 0x2000, 0x3FFC};
	for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
		volatile u32 *const at = (volatile u32 *)(0x70000000 + offsets[i]);
		printf("  %08x reads %08x\n", (u32)at, at[0]);
	}

}

// The registers, which sit in their own window and do not alias memory.
static void testRegisterWindow() {
	printf("The register window:\n");

	static const u32 addresses[] = {0x10000000, 0x1000F000, 0x1000F130,
	                                0x12000000, 0x1F800000};
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		volatile u32 *const at = (volatile u32 *)addresses[i];
		printf("  %08x reads %08x\n", addresses[i], at[0]);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	memset(buffer, 0, sizeof(buffer));
	FlushCache(0);

	testAddresses();
	testAliasing();
	testStaleCache();
	testWrap();
	testScratchpad();
	testRegisterWindow();

	printf("-- TEST END\n");
	return 0;
}
