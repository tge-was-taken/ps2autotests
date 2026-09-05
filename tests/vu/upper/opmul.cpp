#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

// The cross product ops have a shape of their own: opmula takes no
// destination and opmsub reads what it left, so neither fits the fmac runner.
class CrossRunner : public TestRunner {
public:
	CrossRunner(int vu) : TestRunner(vu) {
		loadConstants(vu, edgeValues, edgeValueCount);
		loadConstant(vu, junkIndex, junkBits);
	}

	// Distinct values per lane, so which lane feeds which is readable off the
	// answer rather than assumed.
	void WrLoadLanes(VU::Reg r, int first) {
		using namespace VU;
		for (int lane = 0; lane < 4; ++lane) {
			const int index = (first + lane) % edgeValueCount;
			WrLoadFloatRegister(Dest(DEST_X >> lane), r, constantAt(vu_, index));
		}
	}

	void PerformOpmula(int s, int t) {
		using namespace VU;

		Reset();
		WrLoadLanes(VF01, s);
		WrLoadLanes(VF02, t);
		WrLoadFloatRegister(DEST_XYZW, VF03, constantAt(vu_, junkIndex));
		Wr(OPMULA(VF01, VF02, UPPER_NONE));
		// Reading the accumulator costs a subtract of the zero in VF00.x.
		Wr(MSUBbc(DEST_XYZW, FIELD_X, VF03, VF00, VF00));
		Execute();

		printf("  OPMULA %-5s %-5s: ", edgeValues[s].name, edgeValues[t].name);
		PrintRegisterHex(VF03, true);
	}

	void PerformOpmsub(int s, int t, int acc) {
		using namespace VU;

		Reset();
		WrLoadLanes(VF01, s);
		WrLoadLanes(VF02, t);
		WrLoadFloatRegister(DEST_XYZW, VF03, constantAt(vu_, junkIndex));
		WrLoadFloatRegister(DEST_XYZW, VF04, constantAt(vu_, acc));
		Wr(SUBAbc(DEST_XYZW, FIELD_X, VF04, VF00, UPPER_NONE));
		Wr(OPMSUB(VF03, VF01, VF02, UPPER_NONE));
		Execute();

		printf("  OPMSUB %-5s %-5s acc %-5s: ", edgeValues[s].name,
		       edgeValues[t].name, edgeValues[acc].name);
		PrintRegisterHex(VF03, false);
		PrintStatus(true);
	}

	void PerformPair(int s, int t) {
		using namespace VU;

		Reset();
		WrLoadLanes(VF01, s);
		WrLoadLanes(VF02, t);
		WrLoadFloatRegister(DEST_XYZW, VF03, constantAt(vu_, junkIndex));
		Wr(OPMULA(VF01, VF02, UPPER_NONE));
		Wr(OPMSUB(VF03, VF02, VF01, UPPER_NONE));
		Execute();

		printf("  cross %-5s %-5s: ", edgeValues[s].name, edgeValues[t].name);
		PrintRegisterHex(VF03, false);
		PrintStatus(true);
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	CrossRunner runner(0);

	printf("OPMULA:\n");
	for (int s = 0; s < edgeValueCount; ++s) {
		runner.PerformOpmula(s, (s + 3) % edgeValueCount);
	}

	printf("OPMSUB:\n");
	for (int s = 0; s < edgeValueCount; ++s) {
		runner.PerformOpmsub(s, (s + 3) % edgeValueCount, 2);
	}

	printf("OPMSUB accumulator sweep:\n");
	for (int acc = 0; acc < edgeValueCount; ++acc) {
		runner.PerformOpmsub(2, 4, acc);
	}

	printf("OPMULA then OPMSUB:\n");
	for (int s = 0; s < edgeValueCount; ++s) {
		runner.PerformPair(s, (s + 5) % edgeValueCount);
	}

	printf("-- TEST END\n");
	return 0;
}
