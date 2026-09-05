#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

// Above the constants at 0x440 and clear of the register save area.
static const u32 scratchBase = 0x700;
static const int scratchQuadwords = 8;

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

class LoadStoreRunner : public TestRunner {
public:
	LoadStoreRunner(int vu) : TestRunner(vu) {
		loadConstants(vu, edgeValues, edgeValueCount);
		loadConstant(vu, junkIndex, junkBits);
	}

	u32 *Scratch(int quadword) {
		u8 *base = vu_ == 0 ? vu0_mem : vu1_mem;
		return (u32 *)(base + scratchBase + 16 * quadword);
	}

	u16 ScratchPointer(int quadword) {
		return (scratchBase / 16) + quadword;
	}

	// Each lane different, so a masked lane and a shuffled lane are both
	// obvious in the answer.
	void FillScratch() {
		for (int i = 0; i < scratchQuadwords; ++i) {
			u32 *p = Scratch(i);
			p[0] = 0x11110000 + i;
			p[1] = 0x22220000 + i;
			p[2] = 0x33330000 + i;
			p[3] = 0x44440000 + i;
		}
	}

	// The quadwords either end of vu0 memory, which nothing else in this test
	// uses, so a pointer that wraps lands on something known.
	void FillMarkers() {
		u8 *base = vu_ == 0 ? vu0_mem : vu1_mem;
		for (int i = 0; i < 16; ++i) {
			MarkQuadword(base + 16 * i, i);
			MarkQuadword(base + 16 * (240 + i), 240 + i);
		}
	}

	void MarkQuadword(u8 *at, int index) {
		u32 *p = (u32 *)at;
		p[0] = 0x5A5A0000 + index;
		p[1] = 0x5B5B0000 + index;
		p[2] = 0x5C5C0000 + index;
		p[3] = 0x5D5D0000 + index;
	}

	void PrintScratch(int quadword) {
		const u32 *p = Scratch(quadword);
		printf("%08x %08x %08x %08x", p[0], p[1], p[2], p[3]);
	}

	void PerformLoadMasks() {
		using namespace VU;

		printf("LQ dest masks:\n");
		for (int i = 0; i < destMaskCount; ++i) {
			FillScratch();
			Reset();
			WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, junkIndex));
			Wr(LQ(destMask[i], VF01, VI00, ScratchPointer(3)));
			Execute();

			printf("  %s: ", destMaskName[i]);
			PrintRegisterHex(VF01, true);
		}
	}

	void PerformLoadOffsets() {
		using namespace VU;

		// The immediate is signed, and the base is an integer register, so
		// these say how the two combine and what the sum wraps to.
		static const int offsets[] = {0, 1, 2, 7, -1, -3, 0x3FF, -0x400};
		static const u32 bases[] = {0, 1, 4};

		printf("LQ base and offset:\n");
		for (unsigned b = 0; b < sizeof(bases) / sizeof(bases[0]); ++b) {
			for (unsigned o = 0; o < sizeof(offsets) / sizeof(offsets[0]); ++o) {
				FillScratch();
				Reset();
				WrSetIntegerRegister(VI01, ScratchPointer(0) + bases[b]);
				WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, junkIndex));
				// The encoder wants the raw eleven bits, not a sign extension.
				Wr(LQ(DEST_XYZW, VF01, VI01, offsets[o] & 0x7FF));
				Execute();

				printf("  base +%u offset %+5d: ", bases[b], offsets[o]);
				PrintRegisterHex(VF01, true);
			}
		}
	}

	void PerformLoadIncrement(const char *name, VU::LowerOp (*op)(VU::Dest, VU::Reg, VU::Reg)) {
		using namespace VU;

		printf("%s:\n", name);
		for (int i = 0; i < destMaskCount; ++i) {
			FillScratch();
			Reset();
			WrSetIntegerRegister(VI01, ScratchPointer(4));
			WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, junkIndex));
			Wr(op(destMask[i], VF01, VI01));
			Execute();

			printf("  %s: ", destMaskName[i]);
			PrintRegisterHex(VF01, false);
			printf(" vi=");
			PrintRegister(VI01, true);
		}
	}

	// Whether the pointer an lqi wrote is what the next instruction reads.
	void PerformIncrementVisibility() {
		using namespace VU;

		printf("LQI pointer visible to the next op:\n");
		for (int gap = 0; gap < 4; ++gap) {
			FillScratch();
			Reset();
			WrSetIntegerRegister(VI01, ScratchPointer(0));
			Wr(LQI(DEST_XYZW, VF01, VI01));
			for (int i = 0; i < gap; ++i) {
				Wr(NOP());
			}
			Wr(ILW(DEST_X, VI02, VI01, 0));
			Execute();

			printf("  gap %d: vi01=", gap);
			PrintRegister(VI01, false);
			printf(" vi02=");
			PrintRegister(VI02, true);
		}
	}

	void PerformStoreMasks() {
		using namespace VU;

		printf("SQ dest masks:\n");
		for (int i = 0; i < destMaskCount; ++i) {
			FillScratch();
			Reset();
			WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, junkIndex));
			Wr(SQ(destMask[i], VF01, VI00, ScratchPointer(2)));
			Execute();

			printf("  %s: ", destMaskName[i]);
			PrintScratch(2);
			printf("\n");
		}
	}

	void PerformStoreIncrement(const char *name, VU::LowerOp (*op)(VU::Dest, VU::Reg, VU::Reg)) {
		using namespace VU;

		printf("%s:\n", name);
		for (int i = 0; i < destMaskCount; ++i) {
			FillScratch();
			Reset();
			WrSetIntegerRegister(VI01, ScratchPointer(4));
			WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, junkIndex));
			Wr(op(destMask[i], VF01, VI01));
			Execute();

			printf("  %s: ", destMaskName[i]);
			PrintScratch(3);
			printf(" | ");
			PrintScratch(4);
			printf(" vi=");
			PrintRegister(VI01, true);
		}
	}

	// A pointer past the end of vu memory, which the unit has to fold somehow.
	void PerformWrap() {
		using namespace VU;

		static const u32 pointers[] = {0x000, 0x003, 0x0FF, 0x100, 0x103, 0x1FF, 0x200, 0x3FF};

		printf("LQ past the end of vu memory:\n");
		for (unsigned i = 0; i < sizeof(pointers) / sizeof(pointers[0]); ++i) {
			FillScratch();
			FillMarkers();
			Reset();
			WrLoadFloatRegister(DEST_XYZW, VF01, constantAt(vu_, junkIndex));
			Wr(LQ(DEST_XYZW, VF01, VI00, pointers[i]));
			Execute();

			printf("  ptr %03x: ", pointers[i]);
			PrintRegisterHex(VF01, true);
		}
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	LoadStoreRunner runner(0);

	runner.PerformLoadMasks();
	runner.PerformLoadOffsets();
	runner.PerformLoadIncrement("LQI", &VU::LQI);
	runner.PerformLoadIncrement("LQD", &VU::LQD);
	runner.PerformIncrementVisibility();
	runner.PerformStoreMasks();
	runner.PerformStoreIncrement("SQI", &VU::SQI);
	runner.PerformStoreIncrement("SQD", &VU::SQD);
	runner.PerformWrap();

	printf("-- TEST END\n");
	return 0;
}
