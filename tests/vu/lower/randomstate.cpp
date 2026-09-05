#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

// The random register is a shift register with a seed, so what matters is the
// sequence it produces from a known start and how the three instructions that
// touch it move it along.

static const VU::Reg vfSeed = VU::VF01;
static const VU::Reg vfDest = VU::VF03;

class RandomRunner : public TestRunner {
public:
	RandomRunner() : TestRunner(1) {
		loadConstant(1, junkIndex, junkBits);
	}

	void WrSeed(u32 bits) {
		using namespace VU;
		loadConstant(1, 0, bits);
		WrLoadFloatRegister(DEST_XYZW, vfSeed, constantAt(1, 0));
		WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(1, junkIndex));
		Wr(RINIT(FIELD_X, vfSeed));
	}

	// The first few values from a seed, each into its own register so one run
	// shows the whole sequence.
	void PerformSequence(u32 seed) {
		using namespace VU;

		Reset();
		WrSeed(seed);
		for (int i = 0; i < 6; ++i) {
			Wr(RNEXT(DEST_XYZW, Reg(VF04 + i)));
		}
		Wr(RGET(DEST_XYZW, VF10));
		Execute();

		printf("  seed %08x:", seed);
		for (int i = 0; i < 6; ++i) {
			printf(" ");
			PrintRegisterFieldHex(Reg(VF04 + i), FIELD_X, false);
		}
		printf(" get ");
		PrintRegisterFieldHex(VF10, FIELD_X, true);
	}

	// Reading without advancing, which should give the same value twice.
	void PerformRepeatedGet(u32 seed) {
		using namespace VU;

		Reset();
		WrSeed(seed);
		Wr(RGET(DEST_XYZW, VF04));
		Wr(RGET(DEST_XYZW, VF05));
		Wr(RNEXT(DEST_XYZW, VF06));
		Wr(RGET(DEST_XYZW, VF07));
		Execute();

		printf("  seed %08x: get ", seed);
		PrintRegisterFieldHex(VF04, FIELD_X, false);
		printf(" get ");
		PrintRegisterFieldHex(VF05, FIELD_X, false);
		printf(" next ");
		PrintRegisterFieldHex(VF06, FIELD_X, false);
		printf(" get ");
		PrintRegisterFieldHex(VF07, FIELD_X, true);
	}

	// The exclusive or instruction, which folds a value into the state.
	void PerformXor(u32 seed, u32 value) {
		using namespace VU;

		Reset();
		WrSeed(seed);
		loadConstant(1, 1, value);
		WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(1, 1));
		Wr(RGET(DEST_XYZW, VF04));
		Wr(RXOR(FIELD_X, VF02));
		Wr(RGET(DEST_XYZW, VF05));
		Wr(RNEXT(DEST_XYZW, VF06));
		Execute();

		printf("  seed %08x xor %08x: before ", seed, value);
		PrintRegisterFieldHex(VF04, FIELD_X, false);
		printf(" after ");
		PrintRegisterFieldHex(VF05, FIELD_X, false);
		printf(" next ");
		PrintRegisterFieldHex(VF06, FIELD_X, true);
	}

	// Which lane a field selector reads when the seed register has four
	// different values in it.
	void PerformFields() {
		using namespace VU;

		static const VU::Field fields[] = {FIELD_X, FIELD_Y, FIELD_Z, FIELD_W};
		static const char *const names[] = {"x", "y", "z", "w"};

		printf("RINIT field select:\n");
		for (int f = 0; f < 4; ++f) {
			Reset();
			for (int lane = 0; lane < 4; ++lane) {
				loadConstant(1, 2 + lane, 0x3F800000 + lane);
				WrLoadFloatRegister(Dest(DEST_X >> lane), vfSeed, constantAt(1, 2 + lane));
			}
			WrLoadFloatRegister(DEST_XYZW, vfDest, constantAt(1, junkIndex));
			Wr(RINIT(fields[f], vfSeed));
			Wr(RGET(DEST_XYZW, VF04));
			Execute();

			printf("  %s: ", names[f]);
			PrintRegisterFieldHex(VF04, FIELD_X, true);
		}
	}

	// The destination mask on a read, since the value is one number spread
	// across four lanes.
	void PerformDestMasks() {
		using namespace VU;

		static const VU::Dest masks[] = {DEST_XYZW, DEST_X, DEST_W, DEST_XY,
		                                 DEST_NONE};
		static const char *const names[] = {"xyzw", "x---", "---w", "xy--", "----"};

		printf("RGET destination masks:\n");
		for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
			Reset();
			WrSeed(0x3F800000);
			Wr(RGET(masks[i], VF04));
			Execute();

			printf("  %s: ", names[i]);
			PrintRegisterHex(VF04, true);
		}
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	RandomRunner runner;

	printf("RNEXT from a seed:\n");
	runner.PerformSequence(0x00000000);
	runner.PerformSequence(0x00000001);
	runner.PerformSequence(0x3F800000);
	runner.PerformSequence(0x7FFFFFFF);
	runner.PerformSequence(0xFFFFFFFF);
	runner.PerformSequence(0x12345678);

	printf("RGET without advancing:\n");
	runner.PerformRepeatedGet(0x3F800000);
	runner.PerformRepeatedGet(0x00000001);

	printf("RXOR:\n");
	runner.PerformXor(0x3F800000, 0x00000001);
	runner.PerformXor(0x3F800000, 0xFFFFFFFF);
	runner.PerformXor(0x00000000, 0x3F800000);

	runner.PerformFields();
	runner.PerformDestMasks();

	printf("-- TEST END\n");
	return 0;
}
