#ifndef PS2AUTOTESTS_CPU_VU0_MACRO_MACRO_H
#define PS2AUTOTESTS_CPU_VU0_MACRO_MACRO_H

#include <common-ee.h>
#include <string.h>

// Macro mode runs the same unit the microprograms do, so what these are for is
// the encoding, the transfer either way, and the flags the ee can see.

struct NamedValue {
	const char *name;
	u32 bits;
};

// Fewer than the micro mode tests cross, since the arithmetic is covered there
// and the point here is the path rather than the answer.
static const NamedValue macroValues[] = {
	{"+0",    0x00000000},
	{"-0",    0x80000000},
	{"+1",    0x3F800000},
	{"-1",    0xBF800000},
	{"+2",    0x40000000},
	{"2-eps", 0x3FFFFFFF},
	{"dnmax", 0x007FFFFF},
	{"nrmin", 0x00800000},
	{"max",   0x7F7FFFFF},
	{"-max",  0xFF7FFFFF},
	{"inf",   0x7F800000},
	{"nan",   0x7FFFFFFF},
};

static const int macroValueCount = sizeof(macroValues) / sizeof(macroValues[0]);

static const u32 junkBits = 0x0BADF00D;

// The four lanes differ, so a broadcast names the lane it read and a shuffle
// names where each lane came from.
static const u32 laneValues[4] = {0x3F800000, 0xC0000000, 0x40400000, 0xBE800000};

struct Quad {
	u32 word[4];
} __attribute__((aligned(16)));

static inline void splat(Quad &quad, u32 bits) {
	quad.word[0] = bits;
	quad.word[1] = bits;
	quad.word[2] = bits;
	quad.word[3] = bits;
}

static inline void lanes(Quad &quad, const u32 *values) {
	memcpy(quad.word, values, sizeof(quad.word));
}

static inline void printQuad(const Quad &quad) {
	printf("%08x %08x %08x %08x", quad.word[0], quad.word[1], quad.word[2],
	       quad.word[3]);
}

// Status, mac and clipping, read back after enough delay for them to arrive.
struct Flags {
	u32 status;
	u32 mac;
	u32 clipping;
};

static inline void clearFlags() {
	asm volatile (
		"vnop\n"
		"ctc2 $0, $16\n"
		"ctc2 $0, $17\n"
		"ctc2 $0, $18\n"
		"vnop\n"
		"sync.p\n"
	);
}

static inline void readFlags(Flags &flags) {
	asm volatile (
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"sync.p\n"
		"cfc2 %0, $16\n"
		"cfc2 %1, $17\n"
		"cfc2 %2, $18\n"
		: "=&r"(flags.status), "=&r"(flags.mac), "=&r"(flags.clipping)
	);
}

static inline void printFlags(const Flags &flags) {
	printf(" st %04x mac %04x clip %06x", flags.status & 0xFFFF, flags.mac & 0xFFFF,
	       flags.clipping & 0xFFFFFF);
}

#endif
