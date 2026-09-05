#include "shared.h"

// The same corpus idea for the processor's own float unit.  The generator is a
// shift register with a fixed seed, so the inputs match on every machine.

static const int caseCount = 400;

static u32 randomState;

static u32 nextRandom() {
	randomState ^= randomState << 13;
	randomState ^= randomState >> 17;
	randomState ^= randomState << 5;
	return randomState;
}

// The exponent is chosen apart from the mantissa so the corpus is not all
// middling numbers, and one case in thirty two is a value with its own rule.
static u32 nextFloat() {
	const u32 bits = nextRandom();
	const u32 sign = bits & 0x80000000;
	const u32 mantissa = bits & 0x007FFFFF;
	const u32 choice = (bits >> 23) & 0xFF;

	if (choice < 8) {
		static const u32 special[8] = {
			0x00000000, 0x80000000, 0x7F7FFFFF, 0xFF7FFFFF,
			0x00800000, 0x007FFFFF, 0x7F800000, 0x3F800000};
		return special[choice];
	}
	const u32 exponent = 0x40 + (choice % 0x80);
	return sign | (exponent << 23) | mantissa;
}

#define CORPUS_FUNC(NAME, OP) \
static u32 run_##NAME(u32 a, u32 b, u32 *control) { \
	u32 result; \
	asm volatile ( \
		"ctc1 $0, $31\n" \
		"mtc1 %2, $f1\n" \
		"mtc1 %3, $f2\n" \
		OP " $f3, $f1, $f2\n" \
		"mfc1 %0, $f3\n" \
		"cfc1 %1, $31\n" \
		: "=&r"(result), "=&r"(*control) : "r"(a), "r"(b) \
	); \
	return result; \
}

CORPUS_FUNC(add, "add.s")
CORPUS_FUNC(sub, "sub.s")
CORPUS_FUNC(mul, "mul.s")
CORPUS_FUNC(div, "div.s")

// The accumulating form, seeded from the first operand so the line is enough
// to reproduce the case.
static u32 run_madd(u32 a, u32 b, u32 *control) {
	u32 result;
	asm volatile (
		"ctc1 $0, $31\n"
		"mtc1 %2, $f1\n"
		"mtc1 %3, $f2\n"
		"mtc1 $0, $f4\n"
		"adda.s $f1, $f4\n"
		"madd.s $f3, $f1, $f2\n"
		"mfc1 %0, $f3\n"
		"cfc1 %1, $31\n"
		: "=&r"(result), "=&r"(*control) : "r"(a), "r"(b)
	);
	return result;
}

typedef u32 (*CorpusFunction)(u32, u32, u32 *);

static void run(const char *name, CorpusFunction function, u32 seed) {
	printf("%s:\n", name);
	randomState = seed;
	for (int i = 0; i < caseCount; ++i) {
		const u32 a = nextFloat();
		const u32 b = nextFloat();
		u32 control = 0;
		const u32 result = function(a, b, &control);
		printf("  %08x %08x %08x %08x\n", a, b, result, control);
	}
}

// A square root corpus, which takes one operand rather than two.
static void runSqrt() {
	printf("sqrt.s:\n");
	randomState = 0x7A1B4C3D;
	for (int i = 0; i < caseCount; ++i) {
		const u32 a = nextFloat();
		u32 result;
		u32 control;
		asm volatile (
			"ctc1 $0, $31\n"
			"mtc1 %2, $f1\n"
			"sqrt.s $f3, $f1\n"
			"mfc1 %0, $f3\n"
			"cfc1 %1, $31\n"
			: "=&r"(result), "=&r"(control) : "r"(a)
		);
		printf("  %08x %08x %08x\n", a, result, control);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	run("add.s", &run_add, 0x2463534A);
	run("sub.s", &run_sub, 0x2463534A);
	run("mul.s", &run_mul, 0x2463534A);
	run("div.s", &run_div, 0x3C9E1D57);
	run("madd.s with the accumulator at the first operand", &run_madd, 0x5D1F3A77);
	runSqrt();

	printf("-- TEST END\n");
	return 0;
}
