#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

// The mac register keeps four flags per lane.  Which of them an operation
// raises, and what a lane the destination mask excludes contributes, is what
// this is for.

static const VU::Reg vfLhs = VU::VF01;
static const VU::Reg vfRhs = VU::VF02;
static const VU::Reg vfDest = VU::VF03;

// One value per lane, picked so the four lanes land in four different states.
struct LanePattern {
	const char *name;
	u32 lane[4];
	u32 other[4];
};

static const LanePattern patterns[] = {
	{"zero,neg,over,plain",
	 {0x00000000, 0xBF800000, 0x7F7FFFFF, 0x3F800000},
	 {0x00000000, 0x3F800000, 0x7F7FFFFF, 0x3F800000}},
	{"all zero",
	 {0x00000000, 0x00000000, 0x00000000, 0x00000000},
	 {0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{"all negative",
	 {0xBF800000, 0xC0000000, 0xC0400000, 0xC0800000},
	 {0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000}},
	{"underflow",
	 {0x00800000, 0x00800000, 0x00800000, 0x00800000},
	 {0x33800000, 0x33800000, 0x33800000, 0x33800000}},
	{"signed zeroes",
	 {0x00000000, 0x80000000, 0x00000000, 0x80000000},
	 {0x00000000, 0x00000000, 0x80000000, 0x80000000}},
	{"denormal in",
	 {0x00000001, 0x007FFFFF, 0x00000001, 0x007FFFFF},
	 {0x3F800000, 0x3F800000, 0x00000001, 0x00000000}},
};

static const int patternCount = sizeof(patterns) / sizeof(patterns[0]);

static const VU::Dest destMask[] = {
	VU::DEST_XYZW, VU::DEST_X, VU::DEST_W, VU::DEST_XY,
	VU::Dest(VU::DEST_Y | VU::DEST_Z), VU::DEST_NONE,
};

static const char *const destMaskName[] = {"xyzw", "x---", "---w", "xy--", "-yz-", "----"};

static const int destMaskCount = sizeof(destMask) / sizeof(destMask[0]);

// Somewhere clear of the register save area for the per lane operands.
static const int lhsIndex = 0;
static const int rhsIndex = 4;

class MacRunner : public TestRunner {
public:
	MacRunner() : TestRunner(0) {
		loadConstant(0, junkIndex, junkBits);
	}

	typedef VU::UpperOp (*BinaryOp)(VU::Dest, VU::Reg, VU::Reg, VU::Reg, VU::Flags);
	typedef VU::UpperOp (*UnaryOp)(VU::Dest, VU::Reg, VU::Reg, VU::Flags);

	void LoadPattern(const LanePattern &pattern) {
		for (int lane = 0; lane < 4; ++lane) {
			loadConstant(0, lhsIndex + lane, pattern.lane[lane]);
			loadConstant(0, rhsIndex + lane, pattern.other[lane]);
		}
	}

	void WrLoadLanes() {
		using namespace VU;
		WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(0, junkIndex));
		for (int lane = 0; lane < 4; ++lane) {
			const Dest dest = Dest(DEST_X >> lane);
			WrLoadFloatRegister(dest, vfLhs, constantAt(0, lhsIndex + lane));
			WrLoadFloatRegister(dest, vfRhs, constantAt(0, rhsIndex + lane));
		}
	}

	// The flags arrive a few cycles after the op, so the snapshot waits.
	void WrSnapshot() {
		using namespace VU;
		Wr(NOP());
		Wr(NOP());
		Wr(NOP());
		Wr(FMOR(VI01, VI00));
		Wr(FSOR(VI02, 0));
	}

	void PrintSnapshot() {
		printf(" mac ");
		PrintRegister(VU::VI01, false);
		printf(" status ");
		PrintRegister(VU::VI02, false);
		printf(" result ");
		PrintRegisterHex(vfDest, true);
	}

	void PerformBinary(const char *name, BinaryOp op) {
		using namespace VU;

		printf("%s:\n", name);
		for (int p = 0; p < patternCount; ++p) {
			LoadPattern(patterns[p]);
			Reset();
			WrLoadLanes();
			Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
			WrSnapshot();
			Execute();

			printf("  %-20s", patterns[p].name);
			PrintSnapshot();
		}
	}

	void PerformUnary(const char *name, UnaryOp op) {
		using namespace VU;

		printf("%s:\n", name);
		for (int p = 0; p < patternCount; ++p) {
			LoadPattern(patterns[p]);
			Reset();
			WrLoadLanes();
			Wr(op(DEST_XYZW, vfDest, vfLhs, UPPER_NONE));
			WrSnapshot();
			Execute();

			printf("  %-20s", patterns[p].name);
			PrintSnapshot();
		}
	}

	// A lane the destination mask leaves out: does its flag still move?
	void PerformMasked(const char *name, BinaryOp op, int pattern) {
		using namespace VU;

		printf("%s with %s, destination masks:\n", name, patterns[pattern].name);
		for (int i = 0; i < destMaskCount; ++i) {
			LoadPattern(patterns[pattern]);
			Reset();
			WrLoadLanes();
			Wr(op(destMask[i], vfDest, vfLhs, vfRhs, UPPER_NONE));
			WrSnapshot();
			Execute();

			printf("  %-20s", destMaskName[i]);
			PrintSnapshot();
		}
	}

	// Back to back operations, since the register is rewritten each time
	// rather than accumulated.
	void PerformSequence() {
		using namespace VU;

		printf("MUL then ADD, snapshot after each:\n");
		LoadPattern(patterns[0]);
		Reset();
		WrLoadLanes();
		Wr(MUL(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
		Wr(NOP());
		Wr(NOP());
		Wr(NOP());
		Wr(FMOR(VI03, VI00));
		Wr(ADD(DEST_XYZW, vfDest, vfLhs, vfLhs, UPPER_NONE));
		Wr(NOP());
		Wr(NOP());
		Wr(NOP());
		Wr(FMOR(VI04, VI00));
		Execute();

		printf("  after MUL ");
		PrintRegister(VI03, false);
		printf(", after ADD ");
		PrintRegister(VI04, true);
	}

	// FCSET and FSSET reach the other two registers; nothing writes mac
	// directly, so this says whether it is readable only.
	void PerformStatusSticky() {
		using namespace VU;

		printf("Status after a run of operations:\n");
		LoadPattern(patterns[0]);
		Reset();
		WrLoadLanes();
		Wr(FSSET(0));
		Wr(MUL(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
		Wr(NOP());
		Wr(NOP());
		Wr(NOP());
		Wr(FSOR(VI01, 0));
		Wr(ADD(DEST_XYZW, vfDest, vfLhs, vfLhs, UPPER_NONE));
		Wr(NOP());
		Wr(NOP());
		Wr(NOP());
		Wr(FSOR(VI02, 0));
		Wr(FSSET(0));
		Wr(NOP());
		Wr(NOP());
		Wr(NOP());
		Wr(FSOR(VI03, 0));
		Execute();

		printf("  after MUL ");
		PrintRegister(VI01, false);
		printf(", after ADD ");
		PrintRegister(VI02, false);
		printf(", after FSSET 0 ");
		PrintRegister(VI03, true);
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	MacRunner runner;

	runner.PerformBinary("ADD", &VU::ADD);
	runner.PerformBinary("SUB", &VU::SUB);
	runner.PerformBinary("MUL", &VU::MUL);
	runner.PerformBinary("MAX", &VU::MAX);
	runner.PerformBinary("MINI", &VU::MINI);
	runner.PerformUnary("ABS", &VU::ABS);
	runner.PerformUnary("FTOI0", &VU::FTOI0);
	runner.PerformUnary("ITOF0", &VU::ITOF0);
	runner.PerformMasked("MUL", &VU::MUL, 0);
	runner.PerformMasked("ADD", &VU::ADD, 2);
	runner.PerformSequence();
	runner.PerformStatusSticky();

	printf("-- TEST END\n");
	return 0;
}
