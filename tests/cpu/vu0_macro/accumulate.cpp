#include "macro.h"

// The accumulator has no transfer instruction, so it is read back by a msub
// against the zero in vf0.x, which leaves a negative zero alone where an add
// of zero would not.

#define ACC_FUNC(OP) \
static void run_##OP(Quad &out, const Quad &a, const Quad &b) { \
	asm volatile ( \
		"lqc2 $vf1, 0(%1)\n" \
		"lqc2 $vf2, 0(%2)\n" \
		"lqc2 $vf3, 0(%0)\n" \
		#OP ".xyzw $ACC, $vf1, $vf2\n" \
		"vmsubx.xyzw $vf3, $vf0, $vf0\n" \
		"sqc2 $vf3, 0(%0)\n" \
		: : "r"(&out), "r"(&a), "r"(&b) : "memory" \
	); \
}

ACC_FUNC(vadda)
ACC_FUNC(vsuba)
ACC_FUNC(vmula)
ACC_FUNC(vmadda)
ACC_FUNC(vmsuba)

// madd and msub read the accumulator and write a register, so the accumulator
// is set from a known value first.
#define ACC_READER_FUNC(OP) \
static void run_##OP(Quad &out, const Quad &a, const Quad &b, const Quad &acc) { \
	asm volatile ( \
		"lqc2 $vf1, 0(%1)\n" \
		"lqc2 $vf2, 0(%2)\n" \
		"lqc2 $vf3, 0(%0)\n" \
		"lqc2 $vf4, 0(%3)\n" \
		"vsubax.xyzw $ACC, $vf4, $vf0\n" \
		#OP ".xyzw $vf3, $vf1, $vf2\n" \
		"sqc2 $vf3, 0(%0)\n" \
		: : "r"(&out), "r"(&a), "r"(&b), "r"(&acc) : "memory" \
	); \
}

ACC_READER_FUNC(vmadd)
ACC_READER_FUNC(vmsub)

typedef void (*AccFunction)(Quad &, const Quad &, const Quad &);
typedef void (*AccReaderFunction)(Quad &, const Quad &, const Quad &, const Quad &);

static void testAccumulator(const char *name, AccFunction run) {
	printf("%s:\n", name);
	for (int i = 0; i < macroValueCount; ++i) {
		for (int j = 0; j < macroValueCount; ++j) {
			Quad a, b, out;
			splat(a, macroValues[i].bits);
			splat(b, macroValues[j].bits);
			splat(out, junkBits);

			Flags flags;
			clearFlags();
			run(out, a, b);
			readFlags(flags);

			printf("  %-5s %-5s: %08x", macroValues[i].name, macroValues[j].name,
			       out.word[0]);
			printFlags(flags);
			printf("\n");
		}
	}
}

static void testReader(const char *name, AccReaderFunction run, u32 accBits,
                       const char *accName) {
	printf("%s with acc %s:\n", name, accName);
	for (int i = 0; i < macroValueCount; ++i) {
		for (int j = 0; j < macroValueCount; ++j) {
			Quad a, b, out, acc;
			splat(a, macroValues[i].bits);
			splat(b, macroValues[j].bits);
			splat(out, junkBits);
			splat(acc, accBits);

			Flags flags;
			clearFlags();
			run(out, a, b, acc);
			readFlags(flags);

			printf("  %-5s %-5s: %08x", macroValues[i].name, macroValues[j].name,
			       out.word[0]);
			printFlags(flags);
			printf("\n");
		}
	}
}

// The cross product pair, whose lane mapping is the thing to record.
static void runCross(Quad &out, const Quad &a, const Quad &b) {
	asm volatile (
		"lqc2 $vf1, 0(%1)\n"
		"lqc2 $vf2, 0(%2)\n"
		"lqc2 $vf3, 0(%0)\n"
		"vopmula.xyz $ACC, $vf1, $vf2\n"
		"vopmsub.xyz $vf3, $vf2, $vf1\n"
		"sqc2 $vf3, 0(%0)\n"
		: : "r"(&out), "r"(&a), "r"(&b) : "memory"
	);
}

static void runOpmula(Quad &out, const Quad &a, const Quad &b) {
	asm volatile (
		"lqc2 $vf1, 0(%1)\n"
		"lqc2 $vf2, 0(%2)\n"
		"lqc2 $vf3, 0(%0)\n"
		"vopmula.xyz $ACC, $vf1, $vf2\n"
		"vmsubx.xyzw $vf3, $vf0, $vf0\n"
		"sqc2 $vf3, 0(%0)\n"
		: : "r"(&out), "r"(&a), "r"(&b) : "memory"
	);
}

static const u32 crossOne[4] = {0x3F800000, 0x00000000, 0x00000000, 0x40000000};
static const u32 crossTwo[4] = {0x00000000, 0x3F800000, 0x00000000, 0x40400000};

static void testCrossProduct() {
	Quad a, b, out;

	lanes(a, crossOne);
	lanes(b, crossTwo);
	splat(out, junkBits);
	Flags flags;
	clearFlags();
	runOpmula(out, a, b);
	readFlags(flags);
	printf("vopmula x and y unit vectors: ");
	printQuad(out);
	printFlags(flags);
	printf("\n");

	splat(out, junkBits);
	clearFlags();
	runCross(out, a, b);
	readFlags(flags);
	printf("vopmula then vopmsub: ");
	printQuad(out);
	printFlags(flags);
	printf("\n");

	printf("Cross product per value:\n");
	for (int i = 0; i < macroValueCount; ++i) {
		u32 pattern[4] = {macroValues[i].bits, laneValues[1], laneValues[2],
		                  laneValues[3]};
		lanes(a, pattern);
		lanes(b, laneValues);
		splat(out, junkBits);
		clearFlags();
		runCross(out, a, b);
		readFlags(flags);

		printf("  %-5s: ", macroValues[i].name);
		printQuad(out);
		printFlags(flags);
		printf("\n");
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testAccumulator("vadda", &run_vadda);
	testAccumulator("vsuba", &run_vsuba);
	testAccumulator("vmula", &run_vmula);
	testAccumulator("vmadda", &run_vmadda);
	testAccumulator("vmsuba", &run_vmsuba);

	testReader("vmadd", &run_vmadd, 0x3F800000, "+1");
	testReader("vmsub", &run_vmsub, 0x3F800000, "+1");

	testCrossProduct();

	printf("-- TEST END\n");
	return 0;
}
