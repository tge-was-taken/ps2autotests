#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

// The format itself rather than any one instruction: whether there is an
// infinity, whether there is a nan, what happens either side of the normal
// range, and whether an operation documented as an identity really is one.

static const VU::Reg vfLhs = VU::VF01;
static const VU::Reg vfRhs = VU::VF02;
static const VU::Reg vfDest = VU::VF03;

// A spread of mantissas and exponents, since an identity that fails is
// expected to fail on the bits an operation rounds away.
static const NamedValue spread[] = {
	{"+0",     0x00000000},
	{"-0",     0x80000000},
	{"+1",     0x3F800000},
	{"-1",     0xBF800000},
	{"1+1u",   0x3F800001},
	{"1+2u",   0x3F800002},
	{"1+3u",   0x3F800003},
	{"2-1u",   0x3FFFFFFF},
	{"2-2u",   0x3FFFFFFE},
	{"1.5",    0x3FC00000},
	{"1/3",    0x3EAAAAAB},
	{"1/7",    0x3E124925},
	{"pi",     0x40490FDB},
	{"sqrt2",  0x3FB504F3},
	{"odd1",   0x3F800005},
	{"odd2",   0x40FFFFFF},
	{"tiny",   0x00800001},
	{"nrmin",  0x00800000},
	{"dnmax",  0x007FFFFF},
	{"dnmin",  0x00000001},
	{"max",    0x7F7FFFFF},
	{"max-1u", 0x7F7FFFFE},
	{"-max",   0xFF7FFFFF},
	{"inf",    0x7F800000},
	{"-inf",   0xFF800000},
	{"nan",    0x7FFFFFFF},
	{"qnan",   0x7FC00000},
	{"-nan",   0xFFFFFFFF},
	{"2^-24",  0x33800000},
	{"2^-64",  0x1F800000},
	{"2^64",   0x5F800000},
	{"2^127",  0x7F000000},
};

static const int spreadCount = sizeof(spread) / sizeof(spread[0]);

// Indices into spread.
static const int zero = 0;
static const int negZero = 1;
static const int one = 2;
static const int negOne = 3;
static const int max = 20;
static const int halfIndex = spreadCount;

class FloatRunner : public TestRunner {
public:
	FloatRunner() : TestRunner(0) {
		loadConstants(0, spread, spreadCount);
		loadConstant(0, halfIndex, 0x3F000000);
		loadConstant(0, junkIndex, junkBits);
	}

	typedef VU::UpperOp (*BinaryOp)(VU::Dest, VU::Reg, VU::Reg, VU::Reg, VU::Flags);

	void WrLoad(int s, int t) {
		using namespace VU;
		WrLoadFloatRegister(DEST_XYZW, vfLhs, constantAt(0, s));
		WrLoadFloatRegister(DEST_XYZW, vfRhs, constantAt(0, t));
		WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(0, junkIndex));
	}

	u32 Run(BinaryOp op, int s, int t) {
		using namespace VU;
		Reset();
		WrLoad(s, t);
		Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
		Execute();
		return ReadRegisterField(vfDest, FIELD_X);
	}

	// x op identity, for an op and a value that together should give x back.
	void PerformIdentity(const char *name, BinaryOp op, int other) {
		printf("%s %s:\n", name, spread[other].name);
		for (int s = 0; s < spreadCount; ++s) {
			const u32 result = Run(op, s, other);
			printf("  %-6s in %08x out %08x %s\n", spread[s].name, spread[s].bits,
			       result, result == spread[s].bits ? "same" : "differs");
		}
	}

	// Two of them in a row, which is where a rounding error would show twice.
	void PerformDoubleNegate() {
		using namespace VU;

		printf("MUL by -1 twice:\n");
		for (int s = 0; s < spreadCount; ++s) {
			Reset();
			WrLoad(s, negOne);
			Wr(MUL(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
			Wr(MUL(DEST_XYZW, vfDest, vfDest, vfRhs, UPPER_NONE));
			Execute();

			const u32 result = ReadRegisterField(vfDest, FIELD_X);
			printf("  %-6s in %08x out %08x %s\n", spread[s].name, spread[s].bits,
			       result, result == spread[s].bits ? "same" : "differs");
		}
	}

	// Past the top of the range: whether the answer saturates or grows an
	// exponent the format has no room for.
	void PerformOverflow() {
		using namespace VU;

		static const int left[] = {max, max, 21, 21, 30, 30};
		static const int right[] = {max, one, 21, 2, 30, 20};
		static const char *const opNames[] = {"ADD", "MUL"};
		BinaryOp ops[2] = {&VU::ADD, &VU::MUL};

		printf("Beyond the largest normal:\n");
		for (int o = 0; o < 2; ++o) {
			for (unsigned i = 0; i < sizeof(left) / sizeof(left[0]); ++i) {
				const u32 result = Run(ops[o], left[i], right[i]);
				printf("  %s %-6s %-6s: %08x\n", opNames[o], spread[left[i]].name,
				       spread[right[i]].name, result);
			}
		}
	}

	// Below the smallest normal, where a result has to round to something the
	// format can hold.
	void PerformUnderflow() {
		using namespace VU;

		static const int left[] = {17, 17, 17, 16, 29, 29};
		static const int right[] = {halfIndex, 28, 29, halfIndex, halfIndex, 29};

		printf("Below the smallest normal:\n");
		for (unsigned i = 0; i < sizeof(left) / sizeof(left[0]); ++i) {
			const u32 result = Run(&VU::MUL, left[i], right[i]);
			printf("  MUL %-6s %-6s: %08x\n", spread[left[i]].name,
			       right[i] == halfIndex ? "0.5" : spread[right[i]].name, result);
		}
	}

	// Every combination of the two zeroes through the four ops that have an
	// opinion about a sign.
	void PerformSignedZero() {
		using namespace VU;

		static const int zeroes[] = {zero, negZero};
		static const char *const names[] = {"ADD", "SUB", "MUL", "MAX", "MINI"};
		BinaryOp ops[5] = {&VU::ADD, &VU::SUB, &VU::MUL, &VU::MAX, &VU::MINI};

		printf("Signed zero:\n");
		for (int o = 0; o < 5; ++o) {
			for (int a = 0; a < 2; ++a) {
				for (int b = 0; b < 2; ++b) {
					const u32 result = Run(ops[o], zeroes[a], zeroes[b]);
					printf("  %-4s %-2s %-2s: %08x\n", names[o], spread[zeroes[a]].name,
					       spread[zeroes[b]].name, result);
				}
			}
		}
	}

	// A value the ieee format calls infinity or nan is just a bit pattern
	// here, so this asks what the unit makes of one as an operand.
	void PerformSpecialOperands() {
		using namespace VU;

		static const int specials[] = {23, 24, 25, 26, 27};
		static const char *const names[] = {"ADD", "SUB", "MUL"};
		BinaryOp ops[3] = {&VU::ADD, &VU::SUB, &VU::MUL};

		printf("Infinity and nan patterns:\n");
		for (int o = 0; o < 3; ++o) {
			for (unsigned i = 0; i < sizeof(specials) / sizeof(specials[0]); ++i) {
				for (unsigned j = 0; j < sizeof(specials) / sizeof(specials[0]); ++j) {
					const u32 result = Run(ops[o], specials[i], specials[j]);
					printf("  %-4s %-6s %-6s: %08x\n", names[o], spread[specials[i]].name,
					       spread[specials[j]].name, result);
				}
			}
		}
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	FloatRunner runner;

	runner.PerformIdentity("MUL by", &VU::MUL, one);
	runner.PerformIdentity("ADD", &VU::ADD, zero);
	runner.PerformIdentity("SUB", &VU::SUB, zero);
	runner.PerformIdentity("MAX against", &VU::MAX, negZero);
	runner.PerformDoubleNegate();
	runner.PerformOverflow();
	runner.PerformUnderflow();
	runner.PerformSignedZero();
	runner.PerformSpecialOperands();

	printf("-- TEST END\n");
	return 0;
}
