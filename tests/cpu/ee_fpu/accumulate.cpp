#include "shared.h"

// The accumulator the multiply and add instructions share.  Whether it keeps
// more precision than a register does, and what a chain of them settles on, is
// what a soft implementation has to match.

struct NamedFloat {
	const char *name;
	u32 bits;
};

static const NamedFloat accValues[] = {
	{"+0",    0x00000000},
	{"-0",    0x80000000},
	{"+1",    0x3F800000},
	{"-1",    0xBF800000},
	{"1+1u",  0x3F800001},
	{"2^-24", 0x33800000},
	{"2^-25", 0x33000000},
	{"1/3",   0x3EAAAAAB},
	{"max",   0x7F7FFFFF},
	{"-max",  0xFF7FFFFF},
	{"nrmin", 0x00800000},
	{"dnmax", 0x007FFFFF},
};

static const int accValueCount = sizeof(accValues) / sizeof(accValues[0]);

// The accumulator has no move instruction, so it is read by an add of zero
// into a register.
static u32 readAccumulator() {
	u32 result;
	asm volatile (
		"mtc1 $0, $f4\n"
		"madd.s $f5, $f4, $f4\n"
		"mfc1 %0, $f5\n"
		: "=r"(result)
	);
	return result;
}

#define ACC_FUNC(NAME, OP) \
static u32 run_##NAME(u32 a, u32 b, u32 *control) { \
	u32 result; \
	asm volatile ( \
		"ctc1 $0, $31\n" \
		"mtc1 %2, $f1\n" \
		"mtc1 %3, $f2\n" \
		OP " $f1, $f2\n" \
		"mtc1 $0, $f4\n" \
		"madd.s $f5, $f4, $f4\n" \
		"mfc1 %0, $f5\n" \
		"cfc1 %1, $31\n" \
		: "=&r"(result), "=&r"(*control) : "r"(a), "r"(b) \
	); \
	return result; \
}

ACC_FUNC(adda, "adda.s")
ACC_FUNC(suba, "suba.s")
ACC_FUNC(mula, "mula.s")

// madd and msub read the accumulator as well as writing a register, so it is
// seeded first.
#define ACC_READER_FUNC(NAME, OP) \
static u32 run_##NAME(u32 a, u32 b, u32 seed, u32 *control) { \
	u32 result; \
	asm volatile ( \
		"ctc1 $0, $31\n" \
		"mtc1 %2, $f1\n" \
		"mtc1 %3, $f2\n" \
		"mtc1 %4, $f3\n" \
		"mtc1 $0, $f4\n" \
		"adda.s $f3, $f4\n" \
		OP " $f5, $f1, $f2\n" \
		"mfc1 %0, $f5\n" \
		"cfc1 %1, $31\n" \
		: "=&r"(result), "=&r"(*control) : "r"(a), "r"(b), "r"(seed) \
	); \
	return result; \
}

ACC_READER_FUNC(madd, "madd.s")
ACC_READER_FUNC(msub, "msub.s")

// The accumulating forms name only their two sources, so the answer comes back
// through a read rather than a destination.
#define ACC_ACCUM_FUNC(NAME, OP) \
static u32 run_##NAME(u32 a, u32 b, u32 seed, u32 *control) { \
	u32 result; \
	asm volatile ( \
		"ctc1 $0, $31\n" \
		"mtc1 %2, $f1\n" \
		"mtc1 %3, $f2\n" \
		"mtc1 %4, $f3\n" \
		"mtc1 $0, $f4\n" \
		"adda.s $f3, $f4\n" \
		OP " $f1, $f2\n" \
		"mtc1 $0, $f4\n" \
		"madd.s $f5, $f4, $f4\n" \
		"mfc1 %0, $f5\n" \
		"cfc1 %1, $31\n" \
		: "=&r"(result), "=&r"(*control) : "r"(a), "r"(b), "r"(seed) \
	); \
	return result; \
}

ACC_ACCUM_FUNC(madda, "madda.s")
ACC_ACCUM_FUNC(msuba, "msuba.s")

typedef u32 (*AccFunction)(u32, u32, u32 *);
typedef u32 (*ReaderFunction)(u32, u32, u32, u32 *);

static void testAccumulate(const char *name, AccFunction run) {
	printf("%s:\n", name);
	for (int i = 0; i < accValueCount; ++i) {
		for (int j = 0; j < accValueCount; ++j) {
			u32 control = 0;
			const u32 result = run(accValues[i].bits, accValues[j].bits, &control);
			printf("  %-6s %-6s: %08x fcr31 %08x\n", accValues[i].name,
			       accValues[j].name, result, control);
		}
	}
}

static void testReader(const char *name, ReaderFunction run, u32 seed,
                       const char *seedName) {
	printf("%s with the accumulator at %s:\n", name, seedName);
	for (int i = 0; i < accValueCount; ++i) {
		for (int j = 0; j < accValueCount; ++j) {
			u32 control = 0;
			const u32 result =
				run(accValues[i].bits, accValues[j].bits, seed, &control);
			printf("  %-6s %-6s: %08x fcr31 %08x\n", accValues[i].name,
			       accValues[j].name, result, control);
		}
	}
}

// A product too small to change a register, accumulated many times.  If the
// accumulator kept extra bits, the total would grow; if it rounds every step,
// it would not.
static void testSmallSteps() {
	static const u32 steps[] = {2, 4, 8, 16, 64, 256};

	printf("One plus a small step, accumulated:\n");
	for (unsigned s = 0; s < sizeof(steps) / sizeof(steps[0]); ++s) {
		u32 result;
		// The loop counts the register down, so it is an output as well.
		u32 remaining = steps[s];
		asm volatile (
			"ctc1 $0, $31\n"
			"mtc1 %2, $f1\n"
			"mtc1 %3, $f2\n"
			"mtc1 $0, $f4\n"
			"adda.s $f1, $f4\n"
			"1:\n"
			"madda.s $f2, $f2\n"
			"addiu %1, %1, -1\n"
			"bgtz %1, 1b\n"
			"nop\n"
			"mtc1 $0, $f4\n"
			"madd.s $f5, $f4, $f4\n"
			"mfc1 %0, $f5\n"
			: "=&r"(result), "+r"(remaining)
			: "r"(0x3F800000), "r"(0x39800000)
		);
		printf("  %3u steps of 2^-12 squared: %08x\n", steps[s], result);
	}
}

// The accumulator across a sequence, which is how a dot product is written.
static void testChain() {
	static const u32 pattern[4] = {0x3F800000, 0x40000000, 0x40400000, 0x40800000};

	printf("A four term dot product:\n");
	u32 result;
	asm volatile (
		"ctc1 $0, $31\n"
		"mtc1 %1, $f1\n"
		"mtc1 %2, $f2\n"
		"mtc1 %3, $f3\n"
		"mtc1 %4, $f4\n"
		"mula.s $f1, $f1\n"
		"madda.s $f2, $f2\n"
		"madda.s $f3, $f3\n"
		"madd.s $f5, $f4, $f4\n"
		"mfc1 %0, $f5\n"
		: "=&r"(result)
		: "r"(pattern[0]), "r"(pattern[1]), "r"(pattern[2]), "r"(pattern[3])
	);
	printf("  1 4 9 16 summed: %08x\n", result);
}

// Reading the accumulator without having written it, which says what it holds
// at the start.
static void testInitial() {
	printf("The accumulator before anything writes it: %08x\n", readAccumulator());
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testInitial();
	testAccumulate("adda.s", &run_adda);
	testAccumulate("suba.s", &run_suba);
	testAccumulate("mula.s", &run_mula);
	testReader("madd.s", &run_madd, 0x3F800000, "+1");
	testReader("msub.s", &run_msub, 0x3F800000, "+1");
	testReader("madda.s", &run_madda, 0x3F800000, "+1");
	testReader("msuba.s", &run_msuba, 0x3F800000, "+1");
	testSmallSteps();
	testChain();

	printf("-- TEST END\n");
	return 0;
}
