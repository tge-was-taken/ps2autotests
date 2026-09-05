#include "shared.h"
#include <kernel.h>
#include <string.h>

// The cache instruction and the tag registers behind it.  Only the operations
// that read or invalidate are used: a store tag with a made up value would
// leave the machine holding a line it cannot write back.

static u32 __attribute__((aligned(64))) buffer[4096];

static u32 readTagLo() {
	u32 value;
	asm volatile ("mfc0 %0, $28\n" "sync.p\n" : "=r"(value));
	return value;
}

static u32 readTagHi() {
	u32 value;
	asm volatile ("mfc0 %0, $29\n" "sync.p\n" : "=r"(value));
	return value;
}

static void writeTagLo(u32 value) {
	asm volatile ("mtc0 %0, $28\n" "sync.p\n" : : "r"(value));
}

// The operation field picks the cache and what to do with the line.
#define CACHE_OP(NAME, OP) \
static void NAME(const void *address) { \
	asm volatile ("cache " #OP ", 0(%0)\n" "sync.p\n" : : "r"(address)); \
}

CACHE_OP(indexLoadTagData, 0x11)
CACHE_OP(indexInvalidateData, 0x12)
CACHE_OP(hitInvalidateData, 0x18)
CACHE_OP(indexLoadTagInstruction, 0x05)
CACHE_OP(indexInvalidateInstruction, 0x07)

// Reading a tag at each index says how many lines there are and how the index
// maps onto the address.
static void testDataTags() {
	static const u32 indices[] = {0, 1, 2, 0x3F, 0x40, 0x7F, 0x80, 0xFF};

	printf("Data cache tags by index:\n");
	for (unsigned i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
		writeTagLo(0);
		indexLoadTagData((const void *)(indices[i] * 64));
		printf("  index %3u: taglo %08x taghi %08x\n", indices[i], readTagLo(),
		       readTagHi());
	}
}

static void testInstructionTags() {
	static const u32 indices[] = {0, 1, 0x3F, 0x40, 0xFF, 0x100, 0x1FF};

	printf("Instruction cache tags by index:\n");
	for (unsigned i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
		writeTagLo(0);
		indexLoadTagInstruction((const void *)(indices[i] * 64));
		printf("  index %3u: taglo %08x taghi %08x\n", indices[i], readTagLo(),
		       readTagHi());
	}
}

// A line that has just been read should be present, and a tag read after an
// invalidate should say it is not.
static void testPresence() {
	printf("A line before and after invalidating it:\n");

	volatile u32 *const cached = buffer;
	for (int i = 0; i < 64; ++i) {
		cached[i] = 0x10000 + i;
	}
	FlushCache(0);

	// Touch it, so it is in the cache.
	const u32 touched = cached[0];

	writeTagLo(0);
	indexLoadTagData((const void *)cached);
	const u32 afterTouch = readTagLo();

	hitInvalidateData((const void *)cached);
	writeTagLo(0);
	indexLoadTagData((const void *)cached);
	const u32 afterInvalidate = readTagLo();

	printf("  read %08x, tag after touching %08x, after invalidating %08x\n",
	       touched, afterTouch, afterInvalidate);
}

// A write followed by an invalidate that throws it away, against the same
// write followed by a flush that keeps it.
static void testWriteBack() {
	printf("A dirty line thrown away or written back:\n");

	volatile u32 *const cached = buffer;
	volatile u32 *const uncached =
		(volatile u32 *)(((u32)buffer & 0x1FFFFFFF) | 0xA0000000);

	uncached[0] = 0xAAAAAAAA;
	cached[0] = 0xBBBBBBBB;
	hitInvalidateData((const void *)cached);
	printf("  invalidated: uncached reads %08x\n", uncached[0]);

	uncached[0] = 0xCCCCCCCC;
	cached[0] = 0xDDDDDDDD;
	FlushCache(0);
	printf("  flushed:     uncached reads %08x\n", uncached[0]);
}

// The two synchronising instructions, which have no result of their own but
// must not disturb anything.
static void testSync() {
	printf("Sync:\n");
	volatile u32 *const cached = buffer;
	cached[0] = 0x12345678;
	asm volatile ("sync.l\n");
	const u32 afterL = cached[0];
	asm volatile ("sync.p\n");
	const u32 afterP = cached[0];
	printf("  after sync.l %08x, after sync.p %08x\n", afterL, afterP);
}

// The prefetch instruction, which is allowed to do nothing at all.
static void testPrefetch() {
	printf("Prefetch:\n");
	volatile u32 *const cached = buffer;
	cached[0] = 0x99887766;
	FlushCache(0);
	asm volatile ("pref 0, 0(%0)\n" : : "r"((const void *)cached));

	writeTagLo(0);
	indexLoadTagData((const void *)cached);
	printf("  tag after a prefetch: %08x, value %08x\n", readTagLo(), cached[0]);
}

// What the tag registers hold with nothing having loaded them.
static void testTagRegisters() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0xAAAAAAAA};

	printf("Tag register readback:\n");
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		writeTagLo(values[i]);
		printf("  wrote %08x, taglo %08x taghi %08x\n", values[i], readTagLo(),
		       readTagHi());
	}
	writeTagLo(0);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testTagRegisters();
	testDataTags();
	testInstructionTags();
	testPresence();
	testWriteBack();
	testSync();
	testPrefetch();

	printf("-- TEST END\n");
	return 0;
}
