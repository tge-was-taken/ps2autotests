#include <common-ee.h>
#include <ee_regs.h>
#include <string.h>
#include "../test_runner.h"
#include "../values.h"
#include "../../gs/emit_giftag.h"
#include "../../gs/gsregs.h"

static const u64 noLabel = 0x2222222222222222ULL;

// APATH and OPH: a path is selected, or data is still going out.
static const u32 gifBusy = (3 << 10) | (1 << 9);

// Well clear of the register save area, and low enough that the pointer fits
// wherever a test wants to put it.
static const int packetQuadword = 0x80;

class KickRunner : public TestRunner {
public:
	KickRunner() : TestRunner(1) {
	}

	u32 *Quadword(int index) {
		return (u32 *)(vu1_mem + 16 * index);
	}

	// A packed tag naming one A+D register, then the value and the address it
	// goes to.  Two quadwords, and the second wraps so a packet can start at
	// the last one.
	void WritePacket(int quadword, u32 label) {
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetEop();
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(GIF::REG_AD);

		u64 *at = (u64 *)Quadword(quadword & 0x3FF);
		at[0] = tag.tag_.raw.v0;
		at[1] = tag.tag_.raw.v1;
		u64 *data = (u64 *)Quadword((quadword + 1) & 0x3FF);
		data[0] = GS::LABEL(label, 0xFFFFFFFF);
		data[1] = GS::REG_LABEL;
	}

	// The unit is still reading the packet out when the microprogram ends, so
	// the answer is only there once the path goes quiet.
	void WaitForGif() {
		for (int i = 0; i < 100000; ++i) {
			if ((*R_EE_GIF_STAT & gifBusy) == 0) {
				break;
			}
		}
		for (volatile int i = 0; i < 10000; ++i) {
			continue;
		}
	}

	u32 KickAndReadLabel() {
		*GS::SIGLBLID = noLabel;
		Execute();
		WaitForGif();
		const u64 identifiers = *GS::SIGLBLID;
		*GS::CSR = GS::CSR_SIGNAL;
		return (u32)(identifiers >> 32);
	}

	// Whether the operand counts quadwords, and what a pointer past the end of
	// vu1 memory folds to.
	void PerformAddress() {
		using namespace VU;

		static const int quadwords[] = {0x80, 0x81, 0x3FE, 0x400, 0x401, 0x7FF};

		printf("XGKICK address:\n");
		for (unsigned i = 0; i < sizeof(quadwords) / sizeof(quadwords[0]); ++i) {
			const int quadword = quadwords[i];
			memset(vu1_mem + 0x800, 0, vu1_mem_size - 0x800);
			WritePacket(quadword, 0x1000 + i);

			Reset();
			WrSetIntegerRegister(VI01, quadword);
			Wr(XGKICK(VI01));
			printf("  qword %03x: %04x\n", quadword, KickAndReadLabel());
		}
	}

	// A second kick while the first is still being read out.
	void PerformBackToBack() {
		using namespace VU;

		printf("two XGKICKs, gap in instructions:\n");
		for (int gap = 0; gap < 4; ++gap) {
			memset(vu1_mem + 0x800, 0, vu1_mem_size - 0x800);
			WritePacket(packetQuadword, 0x2001);
			WritePacket(packetQuadword + 2, 0x2002);

			Reset();
			WrSetIntegerRegister(VI01, packetQuadword);
			WrSetIntegerRegister(VI02, packetQuadword + 2);
			Wr(XGKICK(VI01));
			for (int i = 0; i < gap; ++i) {
				Wr(NOP());
			}
			Wr(XGKICK(VI02));
			printf("  gap %d: %04x\n", gap, KickAndReadLabel());
		}
	}

	// A packet whose second quadword falls off the end of vu1 memory.
	void PerformWrap() {
		using namespace VU;

		printf("XGKICK at the last quadword:\n");
		memset(vu1_mem + 0x800, 0, vu1_mem_size - 0x800);

		WritePacket(0x3FF, 0x3003);

		Reset();
		WrSetIntegerRegister(VI01, 0x3FF);
		Wr(XGKICK(VI01));
		printf("  data from quadword 0: %04x\n", KickAndReadLabel());
	}

	// The unit has to be free for the kick to land, so this says what a busy
	// path 3 does to it.
	void PerformStatus() {
		using namespace VU;

		memset(vu1_mem + 0x800, 0, vu1_mem_size - 0x800);
		WritePacket(packetQuadword, 0x4004);

		Reset();
		WrSetIntegerRegister(VI01, packetQuadword);
		Wr(XGKICK(VI01));
		const u32 label = KickAndReadLabel();
		printf("GIF_STAT after a kick: %08x, label %04x\n", *R_EE_GIF_STAT, label);
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	KickRunner runner;

	runner.PerformAddress();
	runner.PerformBackToBack();
	runner.PerformWrap();
	runner.PerformStatus();

	printf("-- TEST END\n");
	return 0;
}
