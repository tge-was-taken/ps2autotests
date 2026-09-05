#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

class DivSqrtRunner : public TestRunner {
public:
	DivSqrtRunner(int vu) : TestRunner(vu) {
		loadConstants(vu, edgeValues, edgeValueCount);
		loadConstant(vu, junkIndex, junkBits);
	}

	// Q only reaches a float register through an add, so VF00.x contributes
	// the zero and VF03.x carries the answer.
	void WrReadQuotient() {
		using namespace VU;
		Wr(WAITQ());
		Wr(ADDq(DEST_XYZW, VF03, VF00, UPPER_NONE));
	}

	void PrintQuotient() {
		PrintRegisterFieldHex(VU::VF03, VU::FIELD_X, false);
		PrintStatus(true);
	}

	void PerformDiv() {
		using namespace VU;

		printf("DIV:\n");
		for (int s = 0; s < edgeValueCount; ++s) {
			for (int t = 0; t < edgeValueCount; ++t) {
				Reset();
				WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, s));
				WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, t));
				WrLoadFloatRegister(DEST_XYZW, VF03, constantAt(vu_, junkIndex));
				Wr(DIV(FIELD_X, VF01, FIELD_X, VF02));
				WrReadQuotient();
				Execute();

				printf("  %-5s / %-5s: ", edgeValues[s].name, edgeValues[t].name);
				PrintQuotient();
			}
		}
	}

	void PerformSqrt() {
		using namespace VU;

		printf("SQRT:\n");
		for (int t = 0; t < edgeValueCount; ++t) {
			Reset();
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, t));
			WrLoadFloatRegister(DEST_XYZW, VF03, constantAt(vu_, junkIndex));
			Wr(SQRT(FIELD_X, VF02));
			WrReadQuotient();
			Execute();

			printf("  %-5s: ", edgeValues[t].name);
			PrintQuotient();
		}
	}

	void PerformRsqrt() {
		using namespace VU;

		printf("RSQRT:\n");
		for (int s = 0; s < edgeValueCount; ++s) {
			for (int t = 0; t < edgeValueCount; ++t) {
				Reset();
				WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, s));
				WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, t));
				WrLoadFloatRegister(DEST_XYZW, VF03, constantAt(vu_, junkIndex));
				Wr(RSQRT(FIELD_X, VF01, FIELD_X, VF02));
				WrReadQuotient();
				Execute();

				printf("  %-5s / sqrt %-5s: ", edgeValues[s].name, edgeValues[t].name);
				PrintQuotient();
			}
		}
	}

	// Which lane each field selector reads, with all four lanes different.
	void PerformFields() {
		using namespace VU;

		static const VU::Field fieldOf[] = {FIELD_X, FIELD_Y, FIELD_Z, FIELD_W};
		static const char *const fieldNames[] = {"x", "y", "z", "w"};

		printf("DIV field select:\n");
		for (int sf = 0; sf < 4; ++sf) {
			for (int tf = 0; tf < 4; ++tf) {
				Reset();
				for (int lane = 0; lane < 4; ++lane) {
					WrLoadFloatRegister(Dest(DEST_X >> lane), VF01, constantAt(vu_, 2 + lane));
					WrLoadFloatRegister(Dest(DEST_X >> lane), VF02, constantAt(vu_, 5 - lane));
				}
				WrLoadFloatRegister(DEST_XYZW, VF03, constantAt(vu_, junkIndex));
				Wr(DIV(fieldOf[sf], VF01, fieldOf[tf], VF02));
				WrReadQuotient();
				Execute();

				printf("  %s / %s: ", fieldNames[sf], fieldNames[tf]);
				PrintQuotient();
			}
		}
	}

	// The status bits a divide by zero raises, and whether they stay raised.
	void PerformStickyStatus() {
		using namespace VU;

		static const int numerators[] = {2, 0, 3};
		static const int denominators[] = {0, 0, 1};

		printf("status after a sequence of divides:\n");
		Reset();
		WrLoadFloatRegister(DEST_XYZW, VF03, constantAt(vu_, junkIndex));
		for (unsigned i = 0; i < sizeof(numerators) / sizeof(numerators[0]); ++i) {
			WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, numerators[i]));
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, denominators[i]));
			Wr(DIV(FIELD_X, VF01, FIELD_X, VF02));
			Wr(WAITQ());
			Wr(FSOR(Reg(VI01 + i), 0));
		}
		Execute();

		for (unsigned i = 0; i < sizeof(numerators) / sizeof(numerators[0]); ++i) {
			printf("  %-5s / %-5s: ", edgeValues[numerators[i]].name,
			       edgeValues[denominators[i]].name);
			PrintRegister(VU::Reg(VU::VI01 + i), true);
		}
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	DivSqrtRunner runner(0);

	runner.PerformDiv();
	runner.PerformSqrt();
	runner.PerformRsqrt();
	runner.PerformFields();
	runner.PerformStickyStatus();

	printf("-- TEST END\n");
	return 0;
}
