#include <common-ee.h>
#include <ee_regs.h>
#include <string.h>
#include "../dma/dmaregs.h"
#include "../dma/dmasend.h"
#include "../gs/emit_giftag.h"
#include "../gs/gsregs.h"
#include "gifprobe.h"

// The control and count registers: what a reset clears, what the pause bit
// holds, and what the three counters say after a packet.

static void printCounters(const char *what) {
	volatile u32 *const count = (volatile u32 *)0x10003040;
	volatile u32 *const path3Count = (volatile u32 *)0x10003050;
	volatile u32 *const path3Tag = (volatile u32 *)0x10003060;

	printf("  %-26s stat %08x cnt %08x p3cnt %08x p3tag %08x\n", what,
	       *R_EE_GIF_STAT, *count, *path3Count, *path3Tag);
}

static void quiet() {
	for (volatile int i = 0; i < 20000; ++i) {
		continue;
	}
}

// Whether a reset clears the status bits a program can set.
static void testReset() {
	printf("Reset:\n");
	printCounters("before");

	*R_EE_GIF_MODE = 3;
	printCounters("mode 3 set");

	*R_EE_GIF_CTRL = 1;
	quiet();
	printCounters("after reset");

	*R_EE_GIF_MODE = 0;
	printCounters("mode cleared");
}

// The pause bit, which should hold a transfer without losing it.
static void testPause() {
	printf("Pause:\n");
	*R_EE_GIF_CTRL = 1;
	quiet();

	GIF::Packet packet(4096);
	GIF::Tag tag;
	tag.SetLoops(200);
	tag.SetFormat(GIF::FORMAT_PACKED);
	tag.SetRegDescs(GIF::REG_NOP);
	packet.WriteTag(tag);
	for (int i = 0; i < 200; ++i) {
		packet.Emit(0);
		packet.Emit(0);
	}
	GIF::WriteProbe(packet);

	*GS::SIGLBLID = GIF::noLabel;
	*R_EE_GIF_CTRL = 1 << 3;
	printCounters("paused, before starting");

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D2->madr = packet.Raw();
	DMA::D2->qwc = packet.Size() / 16;
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR | DMA::CHCR_DIR);
	quiet();
	printCounters("paused, transfer started");
	printf("    label %016llx\n", *GS::SIGLBLID);

	*R_EE_GIF_CTRL = 0;
	int spins = 1000000;
	while (--spins > 0 && DMA::D2->chcr.Ongoing()) {
		continue;
	}
	quiet();
	printCounters("resumed");
	printf("    label %016llx\n", *GS::SIGLBLID);

	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
	*GS::CSR = GS::CSR_SIGNAL;
}

// A reset part way through a packet, which should leave the unit ready for a
// fresh tag rather than expecting the rest of the old one.
static void testResetMidPacket() {
	printf("Reset part way through:\n");
	*R_EE_GIF_CTRL = 1;
	quiet();

	GIF::Packet packet(4096);
	GIF::Tag tag;
	tag.SetLoops(200);
	tag.SetFormat(GIF::FORMAT_PACKED);
	tag.SetRegDescs(GIF::REG_NOP);
	packet.WriteTag(tag);
	for (int i = 0; i < 200; ++i) {
		packet.Emit(0);
		packet.Emit(0);
	}

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D2->madr = packet.Raw();
	DMA::D2->qwc = packet.Size() / 16;
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR | DMA::CHCR_DIR);
	printCounters("mid packet");
	*R_EE_GIF_CTRL = 1;
	quiet();
	printCounters("after reset");
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;

	// A fresh packet, to say whether the unit is usable again.
	GIF::Packet after(256);
	GIF::WriteProbe(after);
	const u64 label = GIF::SendAndReadLabel(after);
	printf("    a packet after the reset %s\n",
	       GIF::ProbeReached(label) ? "landed" : "did not land");
}

// The counter that says how many quadwords have gone through, which a reset
// should put back to zero.
static void testCounters() {
	printf("Counters over three packets:\n");
	*R_EE_GIF_CTRL = 1;
	quiet();
	printCounters("after reset");

	for (int i = 0; i < 3; ++i) {
		GIF::Packet packet(256);
		GIF::WriteProbe(packet);
		GIF::SendAndReadLabel(packet);
		quiet();

		char label[32];
		sprintf(label, "after packet %d", i + 1);
		printCounters(label);
	}
}

// Every bit of the control register on its own, since only two are documented.
static void testControlBits() {
	printf("One control bit at a time:\n");
	for (int bit = 0; bit < 4; ++bit) {
		*R_EE_GIF_CTRL = 1;
		quiet();
		*R_EE_GIF_CTRL = 1u << bit;
		quiet();

		char label[32];
		sprintf(label, "bit %d", bit);
		printCounters(label);
	}
	*R_EE_GIF_CTRL = 1;
	quiet();
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testReset();
	testPause();
	testResetMidPacket();
	testCounters();
	testControlBits();

	printf("-- TEST END\n");
	return 0;
}
