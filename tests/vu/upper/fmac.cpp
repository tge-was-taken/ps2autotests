#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

// A corpus rather than a set of cases.  The generator is a shift register with
// a fixed seed, so the inputs are the same on every machine, and the output is
// one line per case: the two operands, the answer, and the flags.

static const VU::Reg vfLhs = VU::VF01;
static const VU::Reg vfRhs = VU::VF02;
static const VU::Reg vfDest = VU::VF03;

static const int lhsIndex = 0;
static const int rhsIndex = 1;

// Cases per operation.  Four operations at this count is around six thousand
// lines, which is the same order as the largest of the vendored tests.
static const int caseCount = 400;

static u32 randomState;

static u32 nextRandom() {
	randomState ^= randomState << 13;
	randomState ^= randomState >> 17;
	randomState ^= randomState << 5;
	return randomState;
}

// A float drawn from the whole range rather than from a uniform pattern: the
// exponent is chosen separately so small and large values appear as often as
// middling ones.
static u32 nextFloat() {
	const u32 bits = nextRandom();
	const u32 sign = bits & 0x80000000;
	const u32 mantissa = bits & 0x007FFFFF;
	const u32 choice = (bits >> 23) & 0xFF;

	if (choice < 8) {
		// Now and then one of the values that has its own rule.
		static const u32 special[8] = {
			0x00000000, 0x80000000, 0x7F7FFFFF, 0xFF7FFFFF,
			0x00800000, 0x007FFFFF, 0x7F800000, 0x3F800000};
		return special[choice];
	}
	// Otherwise an exponent that keeps the answer inside the normal range
	// most of the time and steps outside it sometimes.
	const u32 exponent = 0x40 + (choice % 0x80);
	return sign | (exponent << 23) | mantissa;
}

class CorpusRunner : public TestRunner {
public:
	CorpusRunner() : TestRunner(0) {
		loadConstant(0, junkIndex, junkBits);
	}

	typedef VU::UpperOp (*BinaryOp)(VU::Dest, VU::Reg, VU::Reg, VU::Reg, VU::Flags);

	void Perform(const char *name, BinaryOp op) {
		using namespace VU;

		printf("%s:\n", name);
		randomState = 0x2463534A;
		for (int i = 0; i < caseCount; ++i) {
			const u32 a = nextFloat();
			const u32 b = nextFloat();
			loadConstant(0, lhsIndex, a);
			loadConstant(0, rhsIndex, b);

			Reset();
			WrLoadFloatRegister(DEST_XYZW, vfLhs, constantAt(0, lhsIndex));
			WrLoadFloatRegister(DEST_XYZW, vfRhs, constantAt(0, rhsIndex));
			WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(0, junkIndex));
			Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			Wr(FMOR(VI01, VI00));
			Wr(FSOR(VI02, 0));
			Execute();

			printf("  %08x %08x ", a, b);
			PrintRegisterFieldHex(vfDest, FIELD_X, false);
			printf(" ");
			PrintRegister(VI01, false);
			printf(" ");
			PrintRegister(VI02, true);
		}
	}

	// The three operand form, with the accumulator seeded from the first
	// operand so the case is reproducible from the two numbers on the line.
	void PerformAccumulate(const char *name, BinaryOp op) {
		using namespace VU;

		printf("%s:\n", name);
		randomState = 0x5D1F3A77;
		for (int i = 0; i < caseCount; ++i) {
			const u32 a = nextFloat();
			const u32 b = nextFloat();
			loadConstant(0, lhsIndex, a);
			loadConstant(0, rhsIndex, b);

			Reset();
			WrLoadFloatRegister(DEST_XYZW, vfLhs, constantAt(0, lhsIndex));
			WrLoadFloatRegister(DEST_XYZW, vfRhs, constantAt(0, rhsIndex));
			WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(0, junkIndex));
			Wr(SUBAbc(DEST_XYZW, FIELD_X, vfLhs, VF00, UPPER_NONE));
			Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			Wr(FMOR(VI01, VI00));
			Wr(FSOR(VI02, 0));
			Execute();

			printf("  %08x %08x ", a, b);
			PrintRegisterFieldHex(vfDest, FIELD_X, false);
			printf(" ");
			PrintRegister(VI01, false);
			printf(" ");
			PrintRegister(VI02, true);
		}
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	CorpusRunner runner;

	runner.Perform("ADD", &VU::ADD);
	runner.Perform("SUB", &VU::SUB);
	runner.Perform("MUL", &VU::MUL);
	runner.PerformAccumulate("MADD with the accumulator at the first operand",
	                         &VU::MADD);

	printf("-- TEST END\n");
	return 0;
}
