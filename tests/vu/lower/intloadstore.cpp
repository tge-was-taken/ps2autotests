#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

static const u32 scratchBase = 0x700;
static const int scratchQuadwords = 8;

// One field at a time: an integer load reads a single word, and what it does
// with more than one bit set is the other half of the question.
static const VU::Dest fieldOf[] = {
	VU::DEST_X, VU::DEST_Y, VU::DEST_Z, VU::DEST_W,
	VU::DEST_XY, VU::DEST_XYZW, VU::DEST_NONE,
};

static const char *const fieldNames[] = {
	"x---", "-y--", "--z-", "---w", "xy--", "xyzw", "----",
};

static const int fieldCount = sizeof(fieldOf) / sizeof(fieldOf[0]);

class IntLoadStoreRunner : public TestRunner {
public:
	IntLoadStoreRunner(int vu) : TestRunner(vu) {
	}

	u32 *Scratch(int quadword) {
		u8 *base = vu_ == 0 ? vu0_mem : vu1_mem;
		return (u32 *)(base + scratchBase + 16 * quadword);
	}

	u16 ScratchPointer(int quadword) {
		return (scratchBase / 16) + quadword;
	}

	// Values whose low and high halves differ, so it is clear which sixteen
	// bits reach the integer register and what a store leaves in the rest.
	void FillScratch() {
		for (int i = 0; i < scratchQuadwords; ++i) {
			u32 *p = Scratch(i);
			p[0] = 0xAAAA1000 + i;
			p[1] = 0xBBBB2000 + i;
			p[2] = 0xCCCC3000 + i;
			p[3] = 0xDDDD4000 + i;
		}
	}

	void PrintScratch(int quadword) {
		const u32 *p = Scratch(quadword);
		printf("%08x %08x %08x %08x", p[0], p[1], p[2], p[3]);
	}

	void PerformLoadFields() {
		using namespace VU;

		printf("ILW fields:\n");
		for (int i = 0; i < fieldCount; ++i) {
			FillScratch();
			Reset();
			WrSetIntegerRegister(VI01, 0x1234);
			Wr(ILW(fieldOf[i], VI01, VI00, ScratchPointer(1)));
			Execute();

			printf("  %s: ", fieldNames[i]);
			PrintRegister(VI01, true);
		}
	}

	void PerformLoadRegister() {
		using namespace VU;

		printf("ILWR fields:\n");
		for (int i = 0; i < fieldCount; ++i) {
			FillScratch();
			Reset();
			WrSetIntegerRegister(VI02, ScratchPointer(2));
			WrSetIntegerRegister(VI01, 0x1234);
			Wr(ILWR(fieldOf[i], VI01, VI02));
			Execute();

			printf("  %s: ", fieldNames[i]);
			PrintRegister(VI01, true);
		}
	}

	void PerformLoadOffsets() {
		using namespace VU;

		static const int offsets[] = {0, 1, 3, -1, -2};

		printf("ILW base and offset:\n");
		for (unsigned o = 0; o < sizeof(offsets) / sizeof(offsets[0]); ++o) {
			FillScratch();
			Reset();
			WrSetIntegerRegister(VI02, ScratchPointer(3));
			WrSetIntegerRegister(VI01, 0x1234);
			// Unlike LQ, the integer loads take a signed offset directly.
			Wr(ILW(DEST_X, VI01, VI02, offsets[o]));
			Execute();

			printf("  offset %+3d: ", offsets[o]);
			PrintRegister(VI01, true);
		}
	}

	void PerformStoreFields() {
		using namespace VU;

		printf("ISW fields:\n");
		for (int i = 0; i < fieldCount; ++i) {
			FillScratch();
			Reset();
			WrSetIntegerRegister(VI01, 0x89AB);
			Wr(ISW(fieldOf[i], VI01, VI00, ScratchPointer(1)));
			Execute();

			printf("  %s: ", fieldNames[i]);
			PrintScratch(1);
			printf("\n");
		}
	}

	void PerformStoreRegister() {
		using namespace VU;

		printf("ISWR fields:\n");
		for (int i = 0; i < fieldCount; ++i) {
			FillScratch();
			Reset();
			WrSetIntegerRegister(VI02, ScratchPointer(2));
			WrSetIntegerRegister(VI01, 0x89AB);
			Wr(ISWR(fieldOf[i], VI01, VI02));
			Execute();

			printf("  %s: ", fieldNames[i]);
			PrintScratch(2);
			printf("\n");
		}
	}

	// An integer register is sixteen bits, so a value with the top bit set is
	// where sign extension either happens or does not.
	void PerformSigns() {
		using namespace VU;

		static const u32 words[] = {
			0x00000000, 0x00007FFF, 0x00008000, 0x0000FFFF,
			0xFFFF0000, 0x12345678, 0xFFFFFFFF, 0x80000000,
		};

		printf("ILW then ISW round trip:\n");
		for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
			FillScratch();
			u32 *p = Scratch(5);
			p[0] = words[i];

			Reset();
			Wr(ILW(DEST_X, VI01, VI00, ScratchPointer(5)));
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			Wr(NOP());
			Wr(ISW(DEST_X, VI01, VI00, ScratchPointer(6)));
			Execute();

			printf("  %08x: vi=", words[i]);
			PrintRegister(VI01, false);
			printf(" stored=");
			PrintScratch(6);
			printf("\n");
		}
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	IntLoadStoreRunner runner(0);

	runner.PerformLoadFields();
	runner.PerformLoadRegister();
	runner.PerformLoadOffsets();
	runner.PerformStoreFields();
	runner.PerformStoreRegister();
	runner.PerformSigns();

	printf("-- TEST END\n");
	return 0;
}
