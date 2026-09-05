#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

// How long the divider and the elementary function unit take, measured by
// reading their result register at every distance behind the operation rather
// than by counting cycles.

static const VU::Reg vfSource = VU::VF01;
static const VU::Reg vfOther = VU::VF02;
static const VU::Reg vfDest = VU::VF03;

class DelayRunner : public TestRunner {
public:
	DelayRunner(int vu) : TestRunner(vu) {
		loadConstants(vu, edgeValues, edgeValueCount);
		loadConstant(vu, junkIndex, junkBits);
		// One half, so a partly finished answer is a different number
		// rather than a zero.
		loadConstant(vu, halfIndex, 0x3F000000);
	}

	static const int halfIndex = 30;

	void WrLoad(int s, int t) {
		using namespace VU;
		WrLoadFloatRegister(DEST_XYZW, vfSource, constantAt(vu_, s));
		WrLoadFloatRegister(DEST_XYZW, vfOther, constantAt(vu_, t));
		WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(vu_, junkIndex));
	}

	// Q only reaches a register through an add, so the read is an addq of the
	// zero the constant register holds.
	void WrReadQuotient(int gap) {
		using namespace VU;
		for (int i = 0; i < gap; ++i) {
			Wr(NOP());
		}
		Wr(ADDq(DEST_XYZW, vfDest, VF00, UPPER_NONE));
	}

	void PerformDivideGaps(const char *name, int s, int t) {
		using namespace VU;

		printf("%s at each gap:\n", name);
		for (int gap = 0; gap <= 10; ++gap) {
			Reset();
			WrLoad(s, t);
			Wr(DIV(FIELD_X, vfSource, FIELD_X, vfOther));
			WrReadQuotient(gap);
			Execute();

			printf("  gap %2d: ", gap);
			PrintRegisterFieldHex(vfDest, FIELD_X, false);
			PrintStatus(true);
		}
	}

	void PerformSqrtGaps(int t) {
		using namespace VU;

		printf("SQRT at each gap:\n");
		for (int gap = 0; gap <= 10; ++gap) {
			Reset();
			WrLoad(t, t);
			Wr(SQRT(FIELD_X, vfSource));
			WrReadQuotient(gap);
			Execute();

			printf("  gap %2d: ", gap);
			PrintRegisterFieldHex(vfDest, FIELD_X, false);
			PrintStatus(true);
		}
	}

	// A second divide issued before the first has finished.
	void PerformOverlap() {
		using namespace VU;

		printf("A second DIV before the first finishes:\n");
		for (int gap = 0; gap <= 8; ++gap) {
			Reset();
			WrLoad(2, halfIndex);
			Wr(DIV(FIELD_X, vfSource, FIELD_X, vfOther));
			for (int i = 0; i < gap; ++i) {
				Wr(NOP());
			}
			Wr(DIV(FIELD_X, vfOther, FIELD_X, vfSource));
			Wr(WAITQ());
			Wr(ADDq(DEST_XYZW, vfDest, VF00, UPPER_NONE));
			Execute();

			printf("  gap %d: ", gap);
			PrintRegisterFieldHex(vfDest, FIELD_X, false);
			PrintStatus(true);
		}
	}

	// The wait instruction, which should make the gap not matter.
	void PerformWait() {
		using namespace VU;

		printf("WAITQ then read, at each gap after it:\n");
		for (int gap = 0; gap <= 4; ++gap) {
			Reset();
			WrLoad(2, halfIndex);
			Wr(DIV(FIELD_X, vfSource, FIELD_X, vfOther));
			Wr(WAITQ());
			WrReadQuotient(gap);
			Execute();

			printf("  gap %d: ", gap);
			PrintRegisterFieldHex(vfDest, FIELD_X, true);
		}
	}

	// The other unit, whose answer arrives in a different register.
	void PerformFunctionGaps(const char *name, VU::LowerOp (*op)(VU::Reg)) {
		using namespace VU;

		printf("%s then MFP at each gap:\n", name);
		for (int gap = 0; gap <= 14; ++gap) {
			Reset();
			WrLoad(2, 2);
			Wr(op(vfSource));
			for (int i = 0; i < gap; ++i) {
				Wr(NOP());
			}
			Wr(MFP(DEST_XYZW, vfDest));
			Execute();

			printf("  gap %2d: ", gap);
			PrintRegisterFieldHex(vfDest, FIELD_X, true);
		}
	}

	void PerformFunctionWait(const char *name, VU::LowerOp (*op)(VU::Reg)) {
		using namespace VU;

		Reset();
		WrLoad(2, 2);
		Wr(op(vfSource));
		Wr(WAITP());
		Wr(MFP(DEST_XYZW, vfDest));
		Execute();

		printf("%s then WAITP: ", name);
		PrintRegisterFieldHex(vfDest, FIELD_X, true);
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	// The elementary function unit is on the second vector unit.
	DelayRunner runner(1);

	runner.PerformDivideGaps("DIV 1 over 0.5", 2, DelayRunner::halfIndex);
	runner.PerformDivideGaps("DIV 1 over 0", 2, 0);
	runner.PerformSqrtGaps(4);
	runner.PerformOverlap();
	runner.PerformWait();
	runner.PerformFunctionGaps("ESUM", &VU::ESUM);
	runner.PerformFunctionGaps("ELENG", &VU::ELENG);
	runner.PerformFunctionWait("ESUM", &VU::ESUM);
	runner.PerformFunctionWait("ELENG", &VU::ELENG);

	printf("-- TEST END\n");
	return 0;
}
