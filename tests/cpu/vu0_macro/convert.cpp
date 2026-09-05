#include "macro.h"

// The fixed point conversions read the same bits two ways: ftoi takes a float
// and itof takes a signed integer, so both directions get the same table.

static const NamedValue conversionValues[] = {
	{"+0",     0x00000000},
	{"-0",     0x80000000},
	{"+1",     0x3F800000},
	{"-1",     0xBF800000},
	{"+0.5",   0x3F000000},
	{"-0.5",   0xBF000000},
	{"1.5",    0x3FC00000},
	{"-1.5",   0xBFC00000},
	{"32767",  0x46FFFE00},
	{"32768",  0x47000000},
	{"2^23",   0x4B000000},
	{"2^31",   0x4F000000},
	{"-2^31",  0xCF000000},
	{"2^32",   0x4F800000},
	{"max",    0x7F7FFFFF},
	{"-max",   0xFF7FFFFF},
	{"i1",     0x00000001},
	{"i-1",    0xFFFFFFFF},
	{"i2^30",  0x40000000},
	{"i-2^30", 0xC0000000},
	{"dnmax",  0x007FFFFF},
	{"inf",    0x7F800000},
};

static const int conversionCount = sizeof(conversionValues) / sizeof(conversionValues[0]);

#define CONVERT_FUNC(OP) \
static void run_##OP(Quad &out, const Quad &a) { \
	asm volatile ( \
		"lqc2 $vf1, 0(%1)\n" \
		"lqc2 $vf2, 0(%0)\n" \
		#OP ".xyzw $vf2, $vf1\n" \
		"sqc2 $vf2, 0(%0)\n" \
		: : "r"(&out), "r"(&a) : "memory" \
	); \
}

CONVERT_FUNC(vftoi0)
CONVERT_FUNC(vftoi4)
CONVERT_FUNC(vftoi12)
CONVERT_FUNC(vftoi15)
CONVERT_FUNC(vitof0)
CONVERT_FUNC(vitof4)
CONVERT_FUNC(vitof12)
CONVERT_FUNC(vitof15)
CONVERT_FUNC(vabs)
CONVERT_FUNC(vmove)
CONVERT_FUNC(vmr32)

typedef void (*ConvertFunction)(Quad &, const Quad &);

static void testConvert(const char *name, ConvertFunction run) {
	printf("%s:\n", name);
	for (int i = 0; i < conversionCount; ++i) {
		Quad a, out;
		splat(a, conversionValues[i].bits);
		splat(out, junkBits);

		Flags flags;
		clearFlags();
		run(out, a);
		readFlags(flags);

		printf("  %-6s: %08x", conversionValues[i].name, out.word[0]);
		printFlags(flags);
		printf("\n");
	}
}

// mr32 rotates the lanes, so a value per lane says which way.
static void testRotate() {
	printf("vmr32 lane order:\n");
	Quad a, out;
	lanes(a, laneValues);
	splat(out, junkBits);
	run_vmr32(out, a);
	printf("  in  ");
	printQuad(a);
	printf("\n  out ");
	printQuad(out);
	printf("\n");

	printf("vmr32 four times:\n");
	Quad current = a;
	for (int i = 0; i < 4; ++i) {
		Quad next;
		splat(next, junkBits);
		run_vmr32(next, current);
		printf("  step %d: ", i + 1);
		printQuad(next);
		printf("\n");
		current = next;
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testConvert("vftoi0", &run_vftoi0);
	testConvert("vftoi4", &run_vftoi4);
	testConvert("vftoi12", &run_vftoi12);
	testConvert("vftoi15", &run_vftoi15);
	testConvert("vitof0", &run_vitof0);
	testConvert("vitof4", &run_vitof4);
	testConvert("vitof12", &run_vitof12);
	testConvert("vitof15", &run_vitof15);
	testConvert("vabs", &run_vabs);
	testConvert("vmove", &run_vmove);
	testRotate();

	printf("-- TEST END\n");
	return 0;
}
