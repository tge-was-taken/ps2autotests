#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

// Indices into edgeValues, picked so each lane of the operand lands in a
// different mac state: zero, negative, overflow and a plain number.
static const int zero = 0;
static const int negOne = 3;
static const int max = 10;
static const int one = 2;

static const u32 scratchBase = 0x700;

class FlagRunner : public TestRunner {
public:
	FlagRunner(int vu) : TestRunner(vu) {
		loadConstants(vu, edgeValues, edgeValueCount);
		loadConstant(vu, junkIndex, junkBits);
	}

	// WrSetIntegerRegister builds a negative value with the wrong magnitude
	// once bit 15 is set, and its results are already recorded on hardware, so
	// a mask that needs all sixteen bits comes through memory instead.
	void WrLoadMask(VU::Reg r, u32 value) {
		using namespace VU;
		u8 *base = vu_ == 0 ? vu0_mem : vu1_mem;
		u32 *at = (u32 *)(base + scratchBase);
		at[0] = value;
		at[1] = value;
		at[2] = value;
		at[3] = value;
		Wr(ILW(DEST_X, r, VI00, scratchBase / 16));
	}

	// One value per lane, so the four mac lanes disagree.
	void WrLoadMixed(VU::Reg r) {
		using namespace VU;
		WrLoadFloatRegister(DEST_X, r, constantAt(vu_, zero));
		WrLoadFloatRegister(DEST_Y, r, constantAt(vu_, negOne));
		WrLoadFloatRegister(DEST_Z, r, constantAt(vu_, max));
		WrLoadFloatRegister(DEST_W, r, constantAt(vu_, one));
	}

	// How long after an fmac op the flags are readable.
	void PerformMacDelay() {
		using namespace VU;

		printf("FMOR after MUL:\n");
		for (int gap = 0; gap < 8; ++gap) {
			Reset();
			WrLoadMixed(VF01);
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, max));
			Wr(MUL(DEST_XYZW, VF03, VF01, VF02, UPPER_NONE));
			for (int i = 0; i < gap; ++i) {
				Wr(NOP());
			}
			Wr(FMOR(VI01, VI00));
			Execute();

			printf("  gap %d: ", gap);
			PrintRegister(VI01, true);
		}
	}

	void PerformMacOps() {
		using namespace VU;

		static const u32 masks[] = {0x0000, 0x000F, 0x00F0, 0x0F00, 0xF000, 0xFFFF, 0x1248};

		printf("FMAND, FMEQ, FMOR against a mask:\n");
		for (unsigned m = 0; m < sizeof(masks) / sizeof(masks[0]); ++m) {
			Reset();
			WrLoadMixed(VF01);
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, max));
			WrLoadMask(VI05, masks[m]);
			Wr(MUL(DEST_XYZW, VF03, VF01, VF02, UPPER_NONE));
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			Wr(FMAND(VI01, VI05));
			Wr(FMEQ(VI02, VI05));
			Wr(FMOR(VI03, VI05));
			Wr(NOP());
			Execute();

			printf("  %04x: and=", masks[m]);
			PrintRegister(VI01, false);
			printf(" eq=");
			PrintRegister(VI02, false);
			printf(" or=");
			PrintRegister(VI03, true);
		}
	}

	void PerformStatusOps() {
		using namespace VU;

		static const u16 masks[] = {0x000, 0x00F, 0x0C0, 0x3F0, 0xFFF, 0x249};

		printf("FSAND, FSEQ, FSOR against a mask:\n");
		for (unsigned m = 0; m < sizeof(masks) / sizeof(masks[0]); ++m) {
			Reset();
			WrLoadMixed(VF01);
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, max));
			Wr(MUL(DEST_XYZW, VF03, VF01, VF02, UPPER_NONE));
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			Wr(FSAND(VI01, masks[m]));
			Wr(FSEQ(VI02, masks[m]));
			Wr(FSOR(VI03, masks[m]));
			Execute();

			printf("  %03x: and=", masks[m]);
			PrintRegister(VI01, false);
			printf(" eq=");
			PrintRegister(VI02, false);
			printf(" or=");
			PrintRegister(VI03, true);
		}
	}

	// Which of the twelve status bits fsset can actually write.
	void PerformStatusSet() {
		using namespace VU;

		printf("FSSET:\n");
		for (int bit = 0; bit < 12; ++bit) {
			Reset();
			Wr(FSSET(u16(1 << bit)));
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			Wr(FSOR(VI01, 0));
			Execute();

			printf("  bit %2d: ", bit);
			PrintRegister(VI01, true);
		}
	}

	void PerformClipOps() {
		using namespace VU;

		static const u32 masks[] = {0x000000, 0x00003F, 0x000FC0, 0xFFFFFF, 0x249249};

		printf("FCAND, FCEQ, FCOR, FCGET after a CLIP:\n");
		for (unsigned m = 0; m < sizeof(masks) / sizeof(masks[0]); ++m) {
			Reset();
			WrLoadMixed(VF01);
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, one));
			Wr(FCSET(0));
			Wr(CLIP(VF01, VF02, UPPER_NONE));
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			// All three write VI01, so each answer moves out of the way.
			Wr(FCAND(VI01, masks[m]));
			Wr(IADD(VI05, VI01, VI00));
			Wr(FCEQ(VI01, masks[m]));
			Wr(IADD(VI06, VI01, VI00));
			Wr(FCOR(VI01, masks[m]));
			Wr(IADD(VI07, VI01, VI00));
			Wr(FCGET(VI04));
			Execute();

			printf("  %06x: and=", masks[m]);
			PrintRegister(VI05, false);
			printf(" eq=");
			PrintRegister(VI06, false);
			printf(" or=");
			PrintRegister(VI07, false);
			printf(" get=");
			PrintRegister(VI04, true);
		}
	}

	// The clip register keeps a history, so a run of clips shifts the older
	// results along.  This says how far.
	void PerformClipHistory() {
		using namespace VU;

		printf("FCGET after repeated CLIP:\n");
		for (int count = 0; count <= 5; ++count) {
			Reset();
			WrLoadMixed(VF01);
			WrLoadFloatRegister(DEST_XYZW, VF02, constantAt(vu_, one));
			Wr(FCSET(0));
			for (int i = 0; i < count; ++i) {
				Wr(CLIP(VF01, VF02, UPPER_NONE));
			}
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			Wr(FCGET(VI01));
			Execute();

			printf("  %d clips: ", count);
			PrintRegister(VI01, true);
		}
	}

	void PerformClipSet() {
		using namespace VU;

		static const u32 values[] = {0x000000, 0x000001, 0x800000, 0xFFFFFF};

		printf("FCSET then FCGET:\n");
		for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			Reset();
			Wr(FCSET(values[v]));
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			Wr(FCGET(VI01));
			Execute();

			printf("  %06x: ", values[v]);
			PrintRegister(VI01, true);
		}
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	FlagRunner runner(0);

	runner.PerformMacDelay();
	runner.PerformMacOps();
	runner.PerformStatusOps();
	runner.PerformStatusSet();
	runner.PerformClipOps();
	runner.PerformClipHistory();
	runner.PerformClipSet();

	printf("-- TEST END\n");
	return 0;
}
