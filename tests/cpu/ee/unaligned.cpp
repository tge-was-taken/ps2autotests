#include "shared.h"
#include <string.h>

// Which address bits a wide access ignores, and what the two halves of an
// unaligned load and store put together at each offset.  Nothing here uses an
// address a narrow load would fault on; those need a handler and live apart.

static u8 __attribute__((aligned(64))) buffer[64];

static void fillBuffer() {
	for (int i = 0; i < 64; ++i) {
		buffer[i] = (u8)(0x10 + i);
	}
}

static void printQuadword(const u128 &value) {
	u32 __attribute__((aligned(16))) words[4];
	*(vu128 *)words = value;
	printf("%08x %08x %08x %08x", words[3], words[2], words[1], words[0]);
}

// lq is documented as ignoring the low four bits of the address.
static void testLoadQuadword() {
	printf("lq at each offset from a 16 byte boundary:\n");
	for (int offset = 0; offset < 17; ++offset) {
		fillBuffer();
		register u128 value;
		asm volatile (
			"lq %0, 0(%1)\n"
			: "=r"(value) : "r"(buffer + 16 + offset)
		);
		printf("  +%2d: ", offset);
		printQuadword(value);
		printf("\n");
	}
}

static void testStoreQuadword() {
	static const u32 __attribute__((aligned(16))) pattern[4] = {
		0xAABBCCDD, 0xEEFF0011, 0x22334455, 0x66778899};

	printf("sq at each offset from a 16 byte boundary:\n");
	for (int offset = 0; offset < 17; ++offset) {
		fillBuffer();
		register u128 value = *(vu128 *)pattern;
		asm volatile (
			"sq %0, 0(%1)\n"
			: : "r"(value), "r"(buffer + 16 + offset) : "memory"
		);
		printf("  +%2d:", offset);
		for (int i = 8; i < 48; i += 4) {
			printf(" %02x%02x%02x%02x", buffer[i + 3], buffer[i + 2], buffer[i + 1],
			       buffer[i]);
		}
		printf("\n");
	}
}

#define UNALIGNED_LOAD(OP) \
static u64 load_##OP(const void *address) { \
	u64 value = 0x1122334455667788ull; \
	asm volatile ( \
		#OP " %0, 0(%1)\n" \
		: "+r"(value) : "r"(address) \
	); \
	return value; \
}

UNALIGNED_LOAD(lwl)
UNALIGNED_LOAD(lwr)
UNALIGNED_LOAD(ldl)
UNALIGNED_LOAD(ldr)

typedef u64 (*LoadFunction)(const void *);

static void testUnalignedLoads() {
	static const LoadFunction runs[] = {&load_lwl, &load_lwr, &load_ldl, &load_ldr};
	static const char *const names[] = {"lwl", "lwr", "ldl", "ldr"};

	printf("Unaligned loads onto a known register:\n");
	for (unsigned r = 0; r < sizeof(runs) / sizeof(runs[0]); ++r) {
		for (int offset = 0; offset < 8; ++offset) {
			fillBuffer();
			printf("  %s +%d: %016llx\n", names[r], offset, runs[r](buffer + 16 + offset));
		}
	}
}

#define UNALIGNED_STORE(OP) \
static void store_##OP(void *address, u64 value) { \
	asm volatile ( \
		#OP " %0, 0(%1)\n" \
		: : "r"(value), "r"(address) : "memory" \
	); \
}

UNALIGNED_STORE(swl)
UNALIGNED_STORE(swr)
UNALIGNED_STORE(sdl)
UNALIGNED_STORE(sdr)

typedef void (*StoreFunction)(void *, u64);

static void testUnalignedStores() {
	static const StoreFunction runs[] = {&store_swl, &store_swr, &store_sdl,
	                                     &store_sdr};
	static const char *const names[] = {"swl", "swr", "sdl", "sdr"};

	printf("Unaligned stores of 0xAABBCCDDEEFF0011:\n");
	for (unsigned r = 0; r < sizeof(runs) / sizeof(runs[0]); ++r) {
		for (int offset = 0; offset < 8; ++offset) {
			fillBuffer();
			runs[r](buffer + 16 + offset, 0xAABBCCDDEEFF0011ull);
			printf("  %s +%d:", names[r], offset);
			for (int i = 12; i < 32; ++i) {
				printf(" %02x", buffer[i]);
			}
			printf("\n");
		}
	}
}

// The two halves used together, which is what a compiler emits for an
// unaligned word.
static void testLoadPairs() {
	printf("lwl then lwr, and ldl then ldr:\n");
	for (int offset = 0; offset < 8; ++offset) {
		fillBuffer();
		u64 word = 0;
		u64 doubleword = 0;
		const u8 *at = buffer + 16 + offset;
		asm volatile (
			"lwl %0, 3(%2)\n"
			"lwr %0, 0(%2)\n"
			"ldl %1, 7(%2)\n"
			"ldr %1, 0(%2)\n"
			: "+r"(word), "+r"(doubleword) : "r"(at)
		);
		printf("  +%d: word %016llx doubleword %016llx\n", offset, word, doubleword);
	}
}

// The scratchpad answers at a different address with the same instructions.
static void testScratchpad() {
	volatile u8 *const scratch = (volatile u8 *)0x70000000;

	printf("lq and sq in the scratchpad:\n");
	for (int i = 0; i < 32; ++i) {
		scratch[i] = (u8)(0xA0 + i);
	}
	for (int offset = 0; offset < 5; ++offset) {
		register u128 value;
		asm volatile (
			"lq %0, 0(%1)\n"
			: "=r"(value) : "r"((const void *)(scratch + offset))
		);
		printf("  +%d: ", offset);
		printQuadword(value);
		printf("\n");
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testLoadQuadword();
	testStoreQuadword();
	testUnalignedLoads();
	testUnalignedStores();
	testLoadPairs();
	testScratchpad();

	printf("-- TEST END\n");
	return 0;
}
