#include "macro.h"

// vf1 and vf2 hold the operands, vf3 the result.  The destination goes in
// carrying junk so a masked off lane is visible.

#define VF3_FUNC(OP) \
static void run_##OP(Quad &out, const Quad &a, const Quad &b) { \
	asm volatile ( \
		"lqc2 $vf1, 0(%1)\n" \
		"lqc2 $vf2, 0(%2)\n" \
		"lqc2 $vf3, 0(%0)\n" \
		#OP ".xyzw $vf3, $vf1, $vf2\n" \
		"sqc2 $vf3, 0(%0)\n" \
		: : "r"(&out), "r"(&a), "r"(&b) : "memory" \
	); \
}

VF3_FUNC(vadd)
VF3_FUNC(vsub)
VF3_FUNC(vmul)
VF3_FUNC(vmax)
VF3_FUNC(vmini)

typedef void (*Vf3Function)(Quad &, const Quad &, const Quad &);

static void testCross(const char *name, Vf3Function run) {
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

// One value per lane, so the four mac lanes have something to disagree about.
static void testLanes(const char *name, Vf3Function run) {
	printf("%s per lane:\n", name);
	for (int i = 0; i < macroValueCount; ++i) {
		Quad a, b, out;
		lanes(a, laneValues);
		splat(b, macroValues[i].bits);
		splat(out, junkBits);

		Flags flags;
		clearFlags();
		run(out, a, b);
		readFlags(flags);

		printf("  %-5s: ", macroValues[i].name);
		printQuad(out);
		printFlags(flags);
		printf("\n");
	}
}

#define VF3_DEST_FUNC(OP, SUFFIX) \
static void run_##OP##_##SUFFIX(Quad &out, const Quad &a, const Quad &b) { \
	asm volatile ( \
		"lqc2 $vf1, 0(%1)\n" \
		"lqc2 $vf2, 0(%2)\n" \
		"lqc2 $vf3, 0(%0)\n" \
		#OP "." #SUFFIX " $vf3, $vf1, $vf2\n" \
		"sqc2 $vf3, 0(%0)\n" \
		: : "r"(&out), "r"(&a), "r"(&b) : "memory" \
	); \
}

VF3_DEST_FUNC(vadd, x)
VF3_DEST_FUNC(vadd, y)
VF3_DEST_FUNC(vadd, z)
VF3_DEST_FUNC(vadd, w)
VF3_DEST_FUNC(vadd, xy)
VF3_DEST_FUNC(vadd, zw)
VF3_DEST_FUNC(vadd, xzw)

static void testDestinationMasks() {
	static const Vf3Function runs[] = {
		&run_vadd_x, &run_vadd_y, &run_vadd_z, &run_vadd_w,
		&run_vadd_xy, &run_vadd_zw, &run_vadd_xzw,
	};
	static const char *const names[] = {"x", "y", "z", "w", "xy", "zw", "xzw"};

	printf("vadd destination masks:\n");
	for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
		Quad a, b, out;
		lanes(a, laneValues);
		splat(b, macroValues[2].bits);
		splat(out, junkBits);

		Flags flags;
		clearFlags();
		runs[i](out, a, b);
		readFlags(flags);

		printf("  %-4s: ", names[i]);
		printQuad(out);
		printFlags(flags);
		printf("\n");
	}
}

// The broadcast forms take one lane of the second operand for all four.
#define VF3_BC_FUNC(OP, BC) \
static void run_##OP##BC(Quad &out, const Quad &a, const Quad &b) { \
	asm volatile ( \
		"lqc2 $vf1, 0(%1)\n" \
		"lqc2 $vf2, 0(%2)\n" \
		"lqc2 $vf3, 0(%0)\n" \
		#OP #BC ".xyzw $vf3, $vf1, $vf2\n" \
		"sqc2 $vf3, 0(%0)\n" \
		: : "r"(&out), "r"(&a), "r"(&b) : "memory" \
	); \
}

VF3_BC_FUNC(vadd, x)
VF3_BC_FUNC(vadd, y)
VF3_BC_FUNC(vadd, z)
VF3_BC_FUNC(vadd, w)
VF3_BC_FUNC(vmul, x)
VF3_BC_FUNC(vmul, w)

static void testBroadcast() {
	static const Vf3Function runs[] = {
		&run_vaddx, &run_vaddy, &run_vaddz, &run_vaddw, &run_vmulx, &run_vmulw,
	};
	static const char *const names[] = {"vaddx", "vaddy", "vaddz", "vaddw",
	                                    "vmulx", "vmulw"};

	printf("Broadcast lane select:\n");
	for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
		Quad a, b, out;
		splat(a, macroValues[2].bits);
		lanes(b, laneValues);
		splat(out, junkBits);

		Flags flags;
		clearFlags();
		runs[i](out, a, b);
		readFlags(flags);

		printf("  %-6s: ", names[i]);
		printQuad(out);
		printFlags(flags);
		printf("\n");
	}
}

// vf0 reads as (0, 0, 0, 1) and cannot be written, which is worth confirming
// from this side of the unit too.
static void testZeroRegister() {
	Quad out;
	splat(out, junkBits);

	asm volatile (
		"sqc2 $vf0, 0(%0)\n"
		: : "r"(&out) : "memory"
	);
	printf("vf0 reads: ");
	printQuad(out);
	printf("\n");

	Quad ones;
	splat(ones, 0x3F800000);
	asm volatile (
		"lqc2 $vf1, 0(%1)\n"
		"vadd.xyzw $vf0, $vf1, $vf1\n"
		"sqc2 $vf0, 0(%0)\n"
		: : "r"(&out), "r"(&ones) : "memory"
	);
	printf("vf0 after a write: ");
	printQuad(out);
	printf("\n");
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testCross("vadd", &run_vadd);
	testCross("vsub", &run_vsub);
	testCross("vmul", &run_vmul);
	testCross("vmax", &run_vmax);
	testCross("vmini", &run_vmini);

	testLanes("vadd", &run_vadd);
	testLanes("vmul", &run_vmul);

	testDestinationMasks();
	testBroadcast();
	testZeroRegister();

	printf("-- TEST END\n");
	return 0;
}
