#include "macro.h"

// The divider writes Q, which the ee reads through cfc2 $22.  Its latency and
// the status bits a division by zero raises are what this records.

static u32 readQuotient() {
	u32 quotient = 0;
	asm volatile (
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"sync.p\n"
		"cfc2 %0, $22\n"
		: "=r"(quotient)
	);
	return quotient;
}

static void runDiv(const Quad &a, const Quad &b) {
	asm volatile (
		"lqc2 $vf1, 0(%0)\n"
		"lqc2 $vf2, 0(%1)\n"
		"vdiv $Q, $vf1x, $vf2x\n"
		: : "r"(&a), "r"(&b)
	);
}

static void runSqrt(const Quad &b) {
	asm volatile (
		"lqc2 $vf2, 0(%0)\n"
		"vsqrt $Q, $vf2x\n"
		: : "r"(&b)
	);
}

static void runRsqrt(const Quad &a, const Quad &b) {
	asm volatile (
		"lqc2 $vf1, 0(%0)\n"
		"lqc2 $vf2, 0(%1)\n"
		"vrsqrt $Q, $vf1x, $vf2x\n"
		: : "r"(&a), "r"(&b)
	);
}

static void testDiv() {
	printf("vdiv:\n");
	for (int i = 0; i < macroValueCount; ++i) {
		for (int j = 0; j < macroValueCount; ++j) {
			Quad a, b;
			splat(a, macroValues[i].bits);
			splat(b, macroValues[j].bits);

			clearFlags();
			runDiv(a, b);
			const u32 quotient = readQuotient();
			Flags flags;
			readFlags(flags);

			printf("  %-5s / %-5s: %08x", macroValues[i].name, macroValues[j].name,
			       quotient);
			printFlags(flags);
			printf("\n");
		}
	}
}

static void testSqrt() {
	printf("vsqrt:\n");
	for (int j = 0; j < macroValueCount; ++j) {
		Quad b;
		splat(b, macroValues[j].bits);

		clearFlags();
		runSqrt(b);
		const u32 quotient = readQuotient();
		Flags flags;
		readFlags(flags);

		printf("  %-5s: %08x", macroValues[j].name, quotient);
		printFlags(flags);
		printf("\n");
	}
}

static void testRsqrt() {
	printf("vrsqrt:\n");
	for (int i = 0; i < macroValueCount; ++i) {
		for (int j = 0; j < macroValueCount; ++j) {
			Quad a, b;
			splat(a, macroValues[i].bits);
			splat(b, macroValues[j].bits);

			clearFlags();
			runRsqrt(a, b);
			const u32 quotient = readQuotient();
			Flags flags;
			readFlags(flags);

			printf("  %-5s / sqrt %-5s: %08x", macroValues[i].name,
			       macroValues[j].name, quotient);
			printFlags(flags);
			printf("\n");
		}
	}
}

// Which lane each field selector reads.
static void testFields() {
	static const char *const names[] = {"x", "y", "z", "w"};

	printf("vdiv field select:\n");
	Quad a, b;
	lanes(a, laneValues);
	splat(b, 0x3F800000);

	u32 quotients[4] = {0, 0, 0, 0};
	asm volatile (
		"lqc2 $vf1, 0(%4)\n"
		"lqc2 $vf2, 0(%5)\n"
		"vdiv $Q, $vf1x, $vf2x\n"
		"vnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\n"
		"cfc2 %0, $22\n"
		"vdiv $Q, $vf1y, $vf2x\n"
		"vnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\n"
		"cfc2 %1, $22\n"
		"vdiv $Q, $vf1z, $vf2x\n"
		"vnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\n"
		"cfc2 %2, $22\n"
		"vdiv $Q, $vf1w, $vf2x\n"
		"vnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\n"
		"cfc2 %3, $22\n"
		: "=&r"(quotients[0]), "=&r"(quotients[1]), "=&r"(quotients[2]),
		  "=&r"(quotients[3])
		: "r"(&a), "r"(&b)
	);
	for (int i = 0; i < 4; ++i) {
		printf("  numerator %s: %08x\n", names[i], quotients[i]);
	}
}

// Reading Q before the divider has finished, at each gap in between.
#define DELAY_FUNC(N, GAP) \
static u32 runDelay##N(const Quad &a, const Quad &b) { \
	u32 quotient = 0; \
	asm volatile ( \
		"lqc2 $vf1, 0(%1)\n" \
		"lqc2 $vf2, 0(%2)\n" \
		"ctc2 $0, $22\n" \
		"vdiv $Q, $vf1x, $vf2x\n" \
		GAP \
		"cfc2 %0, $22\n" \
		: "=&r"(quotient) : "r"(&a), "r"(&b) \
	); \
	return quotient; \
}

DELAY_FUNC(0, "")
DELAY_FUNC(1, "vnop\n")
DELAY_FUNC(2, "vnop\nvnop\n")
DELAY_FUNC(3, "vnop\nvnop\nvnop\nvnop\n")
DELAY_FUNC(4, "vnop\nvnop\nvnop\nvnop\nvnop\nvnop\n")
DELAY_FUNC(5, "vnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\nvnop\n")
DELAY_FUNC(6, "vwaitq\n")

static void testDelay() {
	typedef u32 (*DelayFunction)(const Quad &, const Quad &);
	static const DelayFunction runs[] = {
		&runDelay0, &runDelay1, &runDelay2, &runDelay3, &runDelay4, &runDelay5,
		&runDelay6,
	};
	static const char *const names[] = {"no gap", "1 vnop", "2 vnop", "4 vnop",
	                                    "6 vnop", "8 vnop", "vwaitq"};

	printf("cfc2 $22 after vdiv, 1 over 2:\n");
	Quad a, b;
	splat(a, 0x3F800000);
	splat(b, 0x40000000);
	for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
		printf("  %-7s: %08x\n", names[i], runs[i](a, b));
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testDiv();
	testSqrt();
	testRsqrt();
	testFields();
	testDelay();

	printf("-- TEST END\n");
	return 0;
}
