#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

static const u32 scratchBase = 0x700;

static const VU::Dest destMask[] = {
	VU::DEST_NONE,
	VU::DEST_X, VU::DEST_Y, VU::DEST_Z, VU::DEST_W,
	VU::DEST_XY, VU::DEST_XYZ, VU::DEST_XYZW,
	VU::Dest(VU::DEST_X | VU::DEST_Z),
	VU::Dest(VU::DEST_Y | VU::DEST_W),
};

static const char *const destMaskName[] = {
	"----", "x---", "-y--", "--z-", "---w", "xy--", "xyz-", "xyzw", "x-z-", "-y-w",
};

static const int destMaskCount = sizeof(destMask) / sizeof(destMask[0]);

static const VU::Field fieldOf[] = {VU::FIELD_X, VU::FIELD_Y, VU::FIELD_Z, VU::FIELD_W};
static const char *const fieldNames[] = {"x", "y", "z", "w"};

class MoveRunner : public TestRunner {
public:
	MoveRunner(int vu) : TestRunner(vu) {
		loadConstants(vu, edgeValues, edgeValueCount);
		loadConstant(vu, junkIndex, junkBits);
	}

	u32 *Source() {
		u8 *base = vu_ == 0 ? vu0_mem : vu1_mem;
		return (u32 *)(base + scratchBase);
	}

	// Each lane different, so a rotate names the lane it came from.
	void FillSource() {
		u32 *p = Source();
		p[0] = 0x11111111;
		p[1] = 0x22222222;
		p[2] = 0x33333333;
		p[3] = 0x44444444;
	}

	void PerformMasked(const char *name, VU::LowerOp (*op)(VU::Dest, VU::Reg, VU::Reg)) {
		using namespace VU;

		printf("%s:\n", name);
		for (int i = 0; i < destMaskCount; ++i) {
			FillSource();
			Reset();
			WrLoadFloatRegister(DEST_XYZW, VF01, Source());
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, junkIndex));
			Wr(op(destMask[i], VF02, VF01));
			Execute();

			printf("  %s: ", destMaskName[i]);
			PrintRegisterHex(VF02, true);
		}
	}

	// An integer register is sixteen bits and a float lane is thirty two, so
	// mfir has to widen and mtir has to narrow.
	void PerformMfir() {
		using namespace VU;

		static const u32 integers[] = {0x0000, 0x0001, 0x7FFF, 0x8000, 0xFFFF, 0x1234};

		printf("MFIR:\n");
		for (unsigned v = 0; v < sizeof(integers) / sizeof(integers[0]); ++v) {
			for (int i = 0; i < destMaskCount; ++i) {
				Reset();
				WrSetIntegerRegister(VI01, integers[v]);
				WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, junkIndex));
				Wr(MFIR(destMask[i], VF02, VI01));
				Execute();

				printf("  %04x %s: ", integers[v], destMaskName[i]);
				PrintRegisterHex(VF02, true);
			}
		}
	}

	void PerformMtir() {
		using namespace VU;

		printf("MTIR:\n");
		for (int f = 0; f < 4; ++f) {
			FillSource();
			Reset();
			WrLoadFloatRegister(DEST_XYZW, VF01, Source());
			WrSetIntegerRegister(VI01, 0x1234);
			Wr(MTIR(DEST_XYZW, VI01, fieldOf[f], VF01));
			Execute();

			printf("  from %s: ", fieldNames[f]);
			PrintRegister(VI01, true);
		}

		for (int s = 0; s < edgeValueCount; ++s) {
			Reset();
			WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, s));
			WrSetIntegerRegister(VI01, 0x1234);
			Wr(MTIR(DEST_XYZW, VI01, FIELD_X, VF01));
			Execute();

			printf("  %-5s: ", edgeValues[s].name);
			PrintRegister(VI01, true);
		}
	}

	// The p register holds the last efu answer, and mfp reads it without
	// waiting, so this says how long the answer takes to arrive.
	void PerformMfp() {
		using namespace VU;

		printf("MFP after ESUM:\n");
		for (int gap = 0; gap < 12; ++gap) {
			FillSource();
			Reset();
			WrLoadFloatRegister(DEST_XYZW, VF01, Source());
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, junkIndex));
			Wr(ESUM(VF01));
			for (int i = 0; i < gap; ++i) {
				Wr(NOP());
			}
			Wr(MFP(DEST_XYZW, VF02));
			Execute();

			printf("  gap %2d: ", gap);
			PrintRegisterHex(VF02, true);
		}

		printf("MFP after WAITP:\n");
		for (int i = 0; i < destMaskCount; ++i) {
			FillSource();
			Reset();
			WrLoadFloatRegister(DEST_XYZW, VF01, Source());
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, junkIndex));
			Wr(ESUM(VF01));
			Wr(WAITP());
			Wr(MFP(destMask[i], VF02));
			Execute();

			printf("  %s: ", destMaskName[i]);
			PrintRegisterHex(VF02, true);
		}
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	// The efu that MFP reads lives on vu1.
	MoveRunner runner(1);

	runner.PerformMasked("MOVE", &VU::MOVE);
	runner.PerformMasked("MR32", &VU::MR32);
	runner.PerformMfir();
	runner.PerformMtir();
	runner.PerformMfp();

	printf("-- TEST END\n");
	return 0;
}
