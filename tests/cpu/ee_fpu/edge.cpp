#include "shared.h"

// The format rather than the opcodes: what happens either side of the normal
// range, what a divide by zero leaves behind, which flags stick in fcr31, and
// whether the rounding mode field does anything.

struct NamedFloat {
	const char *name;
	u32 bits;
};

static const NamedFloat edgeFloats[] = {
	{"+0",     0x00000000},
	{"-0",     0x80000000},
	{"+1",     0x3F800000},
	{"-1",     0xBF800000},
	{"+2",     0x40000000},
	{"2-1u",   0x3FFFFFFF},
	{"1+1u",   0x3F800001},
	{"2^-24",  0x33800000},
	{"dnmin",  0x00000001},
	{"dnmax",  0x007FFFFF},
	{"nrmin",  0x00800000},
	{"max",    0x7F7FFFFF},
	{"-max",   0xFF7FFFFF},
	{"inf",    0x7F800000},
	{"-inf",   0xFF800000},
	{"nan",    0x7FFFFFFF},
	{"-nan",   0xFFFFFFFF},
	{"1/3",    0x3EAAAAAB},
	{"pi",     0x40490FDB},
	{"2^64",   0x5F800000},
};

static const int edgeFloatCount = sizeof(edgeFloats) / sizeof(edgeFloats[0]);

static u32 readControl() {
	u32 value;
	asm volatile ("cfc1 %0, $31\n" : "=r"(value));
	return value;
}

static void writeControl(u32 value) {
	asm volatile ("ctc1 %0, $31\n" : : "r"(value));
}

#define BINARY_FUNC(NAME, OP) \
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

BINARY_FUNC(add, "add.s")
BINARY_FUNC(sub, "sub.s")
BINARY_FUNC(mul, "mul.s")
BINARY_FUNC(div, "div.s")

#define UNARY_FUNC(NAME, OP) \
static u32 run_##NAME(u32 a, u32 *control) { \
	u32 result; \
	asm volatile ( \
		"ctc1 $0, $31\n" \
		"mtc1 %2, $f1\n" \
		OP " $f3, $f1\n" \
		"mfc1 %0, $f3\n" \
		"cfc1 %1, $31\n" \
		: "=&r"(result), "=&r"(*control) : "r"(a) \
	); \
	return result; \
}

UNARY_FUNC(sqrt, "sqrt.s")
UNARY_FUNC(abs, "abs.s")
UNARY_FUNC(neg, "neg.s")
UNARY_FUNC(cvtws, "cvt.w.s")
UNARY_FUNC(cvtsw, "cvt.s.w")

static u32 run_rsqrt(u32 a, u32 b, u32 *control) {
	u32 result;
	asm volatile (
		"ctc1 $0, $31\n"
		"mtc1 %2, $f1\n"
		"mtc1 %3, $f2\n"
		"rsqrt.s $f3, $f1, $f2\n"
		"mfc1 %0, $f3\n"
		"cfc1 %1, $31\n"
		: "=&r"(result), "=&r"(*control) : "r"(a), "r"(b)
	);
	return result;
}

typedef u32 (*BinaryFunction)(u32, u32, u32 *);
typedef u32 (*UnaryFunction)(u32, u32 *);

static void testBinary(const char *name, BinaryFunction run) {
	printf("%s:\n", name);
	for (int i = 0; i < edgeFloatCount; ++i) {
		for (int j = 0; j < edgeFloatCount; ++j) {
			u32 control = 0;
			const u32 result = run(edgeFloats[i].bits, edgeFloats[j].bits, &control);
			printf("  %-6s %-6s: %08x fcr31 %08x\n", edgeFloats[i].name,
			       edgeFloats[j].name, result, control);
		}
	}
}

static void testUnary(const char *name, UnaryFunction run) {
	printf("%s:\n", name);
	for (int i = 0; i < edgeFloatCount; ++i) {
		u32 control = 0;
		const u32 result = run(edgeFloats[i].bits, &control);
		printf("  %-6s: %08x fcr31 %08x\n", edgeFloats[i].name, result, control);
	}
}

static void testRsqrt() {
	printf("rsqrt.s:\n");
	for (int i = 0; i < edgeFloatCount; ++i) {
		for (int j = 0; j < edgeFloatCount; ++j) {
			u32 control = 0;
			const u32 result = run_rsqrt(edgeFloats[i].bits, edgeFloats[j].bits, &control);
			printf("  %-6s / sqrt %-6s: %08x fcr31 %08x\n", edgeFloats[i].name,
			       edgeFloats[j].name, result, control);
		}
	}
}

// The rounding mode field is documented as having no effect, since the unit
// only truncates.
static void testRoundingMode() {
	static const u32 modes[] = {0, 1, 2, 3};
	static const u32 values[] = {0x3FC00000, 0xBFC00000, 0x3F400000, 0x40200000};

	printf("Rounding mode against cvt.w.s:\n");
	for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
		printf("  mode %u:", modes[m]);
		for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			u32 result;
			asm volatile (
				"ctc1 %1, $31\n"
				"mtc1 %2, $f1\n"
				"cvt.w.s $f3, $f1\n"
				"mfc1 %0, $f3\n"
				: "=&r"(result) : "r"(modes[m]), "r"(values[v])
			);
			printf(" %08x", result);
		}
		printf(" fcr31 %08x\n", readControl());
		writeControl(0);
	}
}

// Which flag bits accumulate across a run of operations, and which a write
// can clear.
static void testStickyFlags() {
	printf("Flags across a sequence:\n");
	writeControl(0);

	u32 afterDivZero = 0;
	u32 afterQuiet = 0;
	u32 afterClear = 0;
	asm volatile (
		"mtc1 %3, $f1\n"
		"mtc1 %4, $f2\n"
		"div.s $f3, $f1, $f2\n"
		"cfc1 %0, $31\n"
		"add.s $f3, $f1, $f1\n"
		"cfc1 %1, $31\n"
		"ctc1 $0, $31\n"
		"cfc1 %2, $31\n"
		: "=&r"(afterDivZero), "=&r"(afterQuiet), "=&r"(afterClear)
		: "r"(0x3F800000), "r"(0x00000000)
	);
	printf("  after 1/0 %08x, after a quiet add %08x, after ctc1 0 %08x\n",
	       afterDivZero, afterQuiet, afterClear);
}

// Two multiplies whose exact product needs more mantissa than the format has,
// which is where the rounding shows.
static void testRounding() {
	static const NamedFloat rounding[] = {
		{"1+1u",  0x3F800001},
		{"1+2u",  0x3F800002},
		{"1+3u",  0x3F800003},
		{"1.5",   0x3FC00000},
		{"1/3",   0x3EAAAAAB},
		{"1/7",   0x3E124925},
		{"sqrt2", 0x3FB504F3},
		{"m24",   0x4B7FFFFF},
		{"3-1u",  0x403FFFFF},
		{"7",     0x40E00000},
	};
	static const int roundingCount = sizeof(rounding) / sizeof(rounding[0]);

	printf("mul.s rounding:\n");
	for (int i = 0; i < roundingCount; ++i) {
		for (int j = 0; j < roundingCount; ++j) {
			u32 control = 0;
			const u32 result = run_mul(rounding[i].bits, rounding[j].bits, &control);
			printf("  %-6s %-6s: %08x fcr31 %08x\n", rounding[i].name,
			       rounding[j].name, result, control);
		}
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testBinary("add.s", &run_add);
	testBinary("sub.s", &run_sub);
	testBinary("mul.s", &run_mul);
	testBinary("div.s", &run_div);
	testUnary("sqrt.s", &run_sqrt);
	testUnary("abs.s", &run_abs);
	testUnary("neg.s", &run_neg);
	testUnary("cvt.w.s", &run_cvtws);
	testUnary("cvt.s.w", &run_cvtsw);
	testRsqrt();
	testRoundingMode();
	testStickyFlags();
	testRounding();

	printf("-- TEST END\n");
	return 0;
}
