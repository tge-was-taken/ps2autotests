#include <assert.h>
#include "fmac_runner.h"

// VF01 and VF02 hold the operands, VF03 the result, VF04 what Q is divided
// from.  VI01 holds a mac snapshot.  Nothing else in a test touches them.
static const VU::Reg vfLhs = VU::VF01;
static const VU::Reg vfRhs = VU::VF02;
static const VU::Reg vfDest = VU::VF03;
static const VU::Reg vfQuotient = VU::VF04;
static const VU::Reg vfAccSource = VU::VF05;
static const VU::Reg viMacSnapshot = VU::VI01;

struct Pair {
	int s;
	int t;
};

// The pairs worth repeating for each operand source, once the arithmetic has
// had the full cross: both zeroes, both ones, the rounding cases, each class
// against itself, and each class against one.
static const Pair samplePairs[] = {
	{0, 0}, {0, 1}, {1, 0}, {1, 1},
	{2, 2}, {2, 3}, {3, 3},
	{2, 6}, {6, 2}, {5, 5}, {5, 2},
	{7, 7}, {7, 2}, {8, 8}, {8, 2}, {9, 9}, {9, 2},
	{10, 10}, {10, 2}, {10, 11}, {11, 11},
	{12, 12}, {12, 2}, {13, 12},
	{14, 14}, {14, 2}, {15, 14},
};

static const int samplePairCount = sizeof(samplePairs) / sizeof(samplePairs[0]);

static const VU::Dest destMask[] = {
	VU::DEST_NONE,
	VU::DEST_X, VU::DEST_Y, VU::DEST_Z, VU::DEST_W,
	VU::DEST_XY, VU::DEST_XYZ, VU::DEST_XYZW,
	VU::Dest(VU::DEST_X | VU::DEST_Z),
	VU::Dest(VU::DEST_Y | VU::DEST_W),
	VU::Dest(VU::DEST_X | VU::DEST_W),
	VU::Dest(VU::DEST_Y | VU::DEST_Z | VU::DEST_W),
};

static const char *const destMaskName[] = {
	"----", "x---", "-y--", "--z-", "---w",
	"xy--", "xyz-", "xyzw", "x-z-", "-y-w", "x--w", "-yzw",
};

static const int destMaskCount = sizeof(destMask) / sizeof(destMask[0]);

static const VU::Field fieldOf[] = {VU::FIELD_X, VU::FIELD_Y, VU::FIELD_Z, VU::FIELD_W};
static const char *const fieldNames[] = {"x", "y", "z", "w"};

FmacRunner::FmacRunner(int vu) : TestRunner(vu) {
	loadConstant(vu, junkIndex, junkBits);
	UseValues(edgeValues, edgeValueCount);
}

void FmacRunner::UseValues(const NamedValue *values, int count) {
	assert(count >= edgeValueCount);
	values_ = values;
	valueCount_ = count;
	loadConstants(vu_, values, count);
}

void FmacRunner::WrLoadOperands(int s, int t) {
	using namespace VU;
	WrLoadFloatRegister(DEST_XYZW, vfLhs, constantAt(vu_, s));
	WrLoadFloatRegister(DEST_XYZW, vfRhs, constantAt(vu_, t));
	WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(vu_, junkIndex));
}

void FmacRunner::WrSetQuotient(int index) {
	using namespace VU;
	WrLoadFloatRegister(DEST_XYZW, vfQuotient, constantAt(vu_, index));
	Wr(DIV(FIELD_X, vfQuotient, FIELD_W, VF00));
	Wr(WAITQ());
}

void FmacRunner::WrSetAccumulator(int index) {
	using namespace VU;
	WrLoadFloatRegister(DEST_XYZW, vfAccSource, constantAt(vu_, index));
	Wr(SUBAbc(DEST_XYZW, FIELD_X, vfAccSource, VF00, UPPER_NONE));
}

void FmacRunner::WrSnapshotMac() {
	using namespace VU;
	Wr(NOP());
	Wr(NOP());
	Wr(NOP());
	Wr(FMOR(viMacSnapshot, VI00));
}

void FmacRunner::WrReadAccumulator(VU::Reg dest) {
	using namespace VU;
	Wr(MSUBbc(DEST_XYZW, FIELD_X, dest, VF00, VF00));
}

void FmacRunner::PrintResult() {
	PrintRegisterFieldHex(vfDest, VU::FIELD_X, false);
	PrintStatus(true);
}

void FmacRunner::PrintAccumulated() {
	// The mac register printed here belongs to the op under test.  The status
	// line that PrintResult would show belongs to the msub that read the
	// accumulator, which is why this prints the snapshot instead.
	PrintRegisterFieldHex(vfDest, VU::FIELD_X, false);
	printf(" mac=");
	PrintRegister(viMacSnapshot, true);
}

void FmacRunner::PerformPairs(const char *name, BinaryOp op) {
	using namespace VU;

	printf("%s:\n", name);
	for (int s = 0; s < valueCount_; ++s) {
		for (int t = 0; t < valueCount_; ++t) {
			Reset();
			WrLoadOperands(s, t);
			Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
			Execute();

			printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
			PrintResult();
		}
	}
}

void FmacRunner::PerformSample(const char *name, BinaryOp op) {
	using namespace VU;

	printf("%s:\n", name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintResult();
	}
}

void FmacRunner::PerformBroadcast(const char *name, BroadcastOp op, VU::Field bc) {
	using namespace VU;

	printf("%s:\n", name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		Wr(op(DEST_XYZW, bc, vfDest, vfLhs, vfRhs, UPPER_NONE));
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintResult();
	}
}

void FmacRunner::PerformImmediate(const char *name, RegisterOp op) {
	using namespace VU;

	printf("%s:\n", name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		WrImm(values_[t].bits);
		Wr(op(DEST_XYZW, vfDest, vfLhs, UPPER_NONE));
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintResult();
	}
}

void FmacRunner::PerformQuotient(const char *name, RegisterOp op) {
	using namespace VU;

	printf("%s:\n", name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		WrSetQuotient(t);
		Wr(op(DEST_XYZW, vfDest, vfLhs, UPPER_NONE));
		Execute();

		printf("  %-5s q(%-5s): ", values_[s].name, values_[t].name);
		PrintResult();
	}
}

void FmacRunner::PerformAccumulate(const char *name, RegisterOp op) {
	using namespace VU;

	printf("%s:\n", name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		Wr(op(DEST_XYZW, vfLhs, vfRhs, UPPER_NONE));
		WrSnapshotMac();
		WrReadAccumulator(vfDest);
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintAccumulated();
	}
}

void FmacRunner::PerformAccumulateBroadcast(const char *name, BroadcastSingleOp op, VU::Field bc) {
	using namespace VU;

	printf("%s:\n", name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		Wr(op(DEST_XYZW, bc, vfLhs, vfRhs, UPPER_NONE));
		WrSnapshotMac();
		WrReadAccumulator(vfDest);
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintAccumulated();
	}
}

void FmacRunner::PerformAccumulateImmediate(const char *name, SingleOp op) {
	using namespace VU;

	printf("%s:\n", name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		WrImm(values_[t].bits);
		Wr(op(DEST_XYZW, vfLhs, UPPER_NONE));
		WrSnapshotMac();
		WrReadAccumulator(vfDest);
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintAccumulated();
	}
}

void FmacRunner::PerformAccumulateQuotient(const char *name, SingleOp op) {
	using namespace VU;

	printf("%s:\n", name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		WrSetQuotient(t);
		Wr(op(DEST_XYZW, vfLhs, UPPER_NONE));
		WrSnapshotMac();
		WrReadAccumulator(vfDest);
		Execute();

		printf("  %-5s q(%-5s): ", values_[s].name, values_[t].name);
		PrintAccumulated();
	}
}

void FmacRunner::PerformUnary(const char *name, RegisterOp op) {
	using namespace VU;

	printf("%s:\n", name);
	for (int s = 0; s < valueCount_; ++s) {
		Reset();
		WrLoadOperands(s, s);
		Wr(op(DEST_XYZW, vfDest, vfLhs, UPPER_NONE));
		Execute();

		printf("  %-5s: ", values_[s].name);
		PrintResult();
	}
}

void FmacRunner::PerformBroadcastLanes(const char *name, BroadcastOp op) {
	using namespace VU;

	printf("%s lane select:\n", name);
	for (int f = 0; f < 4; ++f) {
		Reset();

		// A different edge value in each lane of the right operand, so the
		// answer names the lane the broadcast read.
		WrLoadFloatRegister(DEST_XYZW, vfLhs, constantAt(vu_, 2));
		WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(vu_, junkIndex));
		for (int lane = 0; lane < 4; ++lane) {
			WrLoadFloatRegister(Dest(DEST_X >> lane), vfRhs, constantAt(vu_, 2 + lane));
		}
		Wr(op(DEST_XYZW, fieldOf[f], vfDest, vfLhs, vfRhs, UPPER_NONE));
		Execute();

		printf("  bc %s: ", fieldNames[f]);
		PrintRegisterHex(vfDest, false);
		PrintStatus(true);
	}
}

void FmacRunner::PerformDestMasks(const char *name, BinaryOp op, int s, int t) {
	using namespace VU;

	printf("%s dest masks, %s and %s:\n", name, values_[s].name, values_[t].name);
	for (int i = 0; i < destMaskCount; ++i) {
		Reset();
		WrLoadOperands(s, t);
		Wr(op(destMask[i], vfDest, vfLhs, vfRhs, UPPER_NONE));
		Execute();

		printf("  %s: ", destMaskName[i]);
		PrintRegisterHex(vfDest, false);
		PrintStatus(true);
	}
}

void FmacRunner::PerformLanes(const char *name, BinaryOp op) {
	using namespace VU;

	printf("%s per lane:\n", name);
	for (int s = 0; s < valueCount_; ++s) {
		Reset();

		WrLoadFloatRegister(DEST_XYZW, vfLhs, constantAt(vu_, s));
		WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(vu_, junkIndex));
		for (int lane = 0; lane < 4; ++lane) {
			const int t = (s + lane) % valueCount_;
			WrLoadFloatRegister(Dest(DEST_X >> lane), vfRhs, constantAt(vu_, t));
		}
		Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
		Execute();

		printf("  %-5s: ", values_[s].name);
		PrintRegisterHex(vfDest, false);
		PrintStatus(true);
	}
}

void FmacRunner::PerformQuotientValues() {
	using namespace VU;

	printf("q after dividing by one:\n");
	for (int s = 0; s < valueCount_; ++s) {
		Reset();
		WrLoadOperands(s, s);
		WrSetQuotient(s);
		Wr(ADDq(DEST_XYZW, vfDest, VF00, UPPER_NONE));
		Execute();

		printf("  %-5s: ", values_[s].name);
		PrintResult();
	}
}

void FmacRunner::PerformPairsWithAccumulator(const char *name, BinaryOp op, int acc) {
	using namespace VU;

	printf("%s, acc %s:\n", name, values_[acc].name);
	for (int s = 0; s < valueCount_; ++s) {
		for (int t = 0; t < valueCount_; ++t) {
			Reset();
			WrLoadOperands(s, t);
			WrSetAccumulator(acc);
			Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
			Execute();

			printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
			PrintResult();
		}
	}
}

void FmacRunner::PerformSampleWithAccumulator(const char *name, BinaryOp op, int acc) {
	using namespace VU;

	printf("%s, acc %s:\n", name, values_[acc].name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		WrSetAccumulator(acc);
		Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintResult();
	}
}

void FmacRunner::PerformBroadcastWithAccumulator(const char *name, BroadcastOp op, VU::Field bc, int acc) {
	using namespace VU;

	printf("%s, acc %s:\n", name, values_[acc].name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		WrSetAccumulator(acc);
		Wr(op(DEST_XYZW, bc, vfDest, vfLhs, vfRhs, UPPER_NONE));
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintResult();
	}
}

void FmacRunner::PerformImmediateWithAccumulator(const char *name, RegisterOp op, int acc) {
	using namespace VU;

	printf("%s, acc %s:\n", name, values_[acc].name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		WrSetAccumulator(acc);
		WrImm(values_[t].bits);
		Wr(op(DEST_XYZW, vfDest, vfLhs, UPPER_NONE));
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintResult();
	}
}

void FmacRunner::PerformQuotientWithAccumulator(const char *name, RegisterOp op, int acc) {
	using namespace VU;

	printf("%s, acc %s:\n", name, values_[acc].name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		WrSetAccumulator(acc);
		WrSetQuotient(t);
		Wr(op(DEST_XYZW, vfDest, vfLhs, UPPER_NONE));
		Execute();

		printf("  %-5s q(%-5s): ", values_[s].name, values_[t].name);
		PrintResult();
	}
}

void FmacRunner::PerformAccumulatorSweep(const char *name, BinaryOp op, int s, int t) {
	using namespace VU;

	printf("%s accumulator sweep, %s and %s:\n", name, values_[s].name, values_[t].name);
	for (int acc = 0; acc < valueCount_; ++acc) {
		Reset();
		WrLoadOperands(s, t);
		WrSetAccumulator(acc);
		Wr(op(DEST_XYZW, vfDest, vfLhs, vfRhs, UPPER_NONE));
		Execute();

		printf("  acc %-5s: ", values_[acc].name);
		PrintResult();
	}
}

void FmacRunner::PerformAccumulateWithAccumulator(const char *name, RegisterOp op, int acc) {
	using namespace VU;

	printf("%s, acc %s:\n", name, values_[acc].name);
	for (int i = 0; i < samplePairCount; ++i) {
		const int s = samplePairs[i].s;
		const int t = samplePairs[i].t;

		Reset();
		WrLoadOperands(s, t);
		WrSetAccumulator(acc);
		Wr(op(DEST_XYZW, vfLhs, vfRhs, UPPER_NONE));
		WrSnapshotMac();
		WrReadAccumulator(vfDest);
		Execute();

		printf("  %-5s %-5s: ", values_[s].name, values_[t].name);
		PrintAccumulated();
	}
}
