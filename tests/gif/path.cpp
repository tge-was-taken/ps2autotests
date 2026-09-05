#include <common-ee.h>
#include <ee_regs.h>
#include <string.h>
#include "../dma/dmaregs.h"
#include "../dma/dmasend.h"
#include "../dma/vif/emit_vifcode.h"
#include "../dma/vif/vifunits.h"
#include "../gs/emit_giftag.h"
#include "../gs/gsregs.h"
#include "gifprobe.h"

// Three sources feed the unit and only one at a time gets through.  What the
// status register says while each is running, and what masking path three
// does to a transfer already under way, is what this records.

static void printStatus(const char *what) {
	const u32 status = *R_EE_GIF_STAT;
	printf("  %-26s stat %08x m3r %d m3p %d imt %d p3q %d p2q %d p1q %d oph %d"
	       " apath %d fqc %d\n",
	       what, status, (status >> 0) & 1, (status >> 1) & 1, (status >> 2) & 1,
	       (status >> 3) & 1, (status >> 4) & 1, (status >> 5) & 1,
	       (status >> 9) & 1, (status >> 10) & 3, (status >> 24) & 0x1F);
}

static void quiet() {
	for (volatile int i = 0; i < 20000; ++i) {
		continue;
	}
}

// Path three is the controller channel, path two comes through vif1 as a
// direct transfer, and path one is a kick from the vector unit.
static void testPathThree() {
	printf("Path 3, the controller channel:\n");
	printStatus("before");

	GIF::Packet packet(256);
	GIF::WriteProbe(packet);
	const u64 label = GIF::SendAndReadLabel(packet);
	printStatus("after");
	printf("  probe %s\n", GIF::ProbeReached(label) ? "reached" : "not reached");
}

static void testPathTwo() {
	printf("Path 2, through vif1 as a direct transfer:\n");

	VIF::Unit1.regs->fbrst = VIF::FBRST_RST;
	*GS::SIGLBLID = GIF::noLabel;

	GIF::Tag tag;
	tag.SetLoops(1);
	tag.SetEop();
	tag.SetFormat(GIF::FORMAT_PACKED);
	tag.SetRegDescs(GIF::REG_AD);

	VIF::Packet packet(256);
	packet.DIRECT(2);
	packet.Data32((u32)tag.tag_.raw.v0);
	packet.Data32((u32)(tag.tag_.raw.v0 >> 32));
	packet.Data32((u32)tag.tag_.raw.v1);
	packet.Data32((u32)(tag.tag_.raw.v1 >> 32));
	packet.Data32((u32)GS::LABEL(0x1234, 0xFFFFFFFF));
	packet.Data32((u32)(GS::LABEL(0x1234, 0xFFFFFFFF) >> 32));
	packet.Data32(GS::REG_LABEL);
	packet.Data32(0);
	packet.NOP();
	packet.NOP();
	packet.NOP();

	DMA::SendSimple(DMA::D1, packet.Raw(), 48);
	quiet();
	printStatus("after");
	printf("  label %016llx\n", *GS::SIGLBLID);
	*GS::CSR = GS::CSR_SIGNAL;
}

// The mask stops path three between packets rather than mid packet.
static void testMaskPathThree() {
	printf("Masking path 3:\n");

	VIF::Unit1.regs->fbrst = VIF::FBRST_RST;
	{
		VIF::Packet mask(64);
		mask.MSKPATH3(true);
		mask.NOP();
		mask.NOP();
		mask.NOP();
		DMA::SendSimple(DMA::D1, mask.Raw(), 16);
	}
	printStatus("masked");

	*GS::SIGLBLID = GIF::noLabel;
	GIF::Packet packet(256);
	GIF::WriteProbe(packet);

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D2->madr = packet.Raw();
	DMA::D2->qwc = packet.Size() / 16;
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR | DMA::CHCR_DIR);
	quiet();
	printStatus("while masked, mid transfer");
	printf("  label %016llx\n", *GS::SIGLBLID);

	{
		VIF::Packet unmask(64);
		unmask.MSKPATH3(false);
		unmask.NOP();
		unmask.NOP();
		unmask.NOP();
		DMA::SendSimple(DMA::D1, unmask.Raw(), 16);
	}
	quiet();

	int spins = 1000000;
	while (--spins > 0 && DMA::D2->chcr.Ongoing()) {
		continue;
	}
	printStatus("unmasked");
	printf("  label %016llx\n", *GS::SIGLBLID);
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
	*GS::CSR = GS::CSR_SIGNAL;
}

// The mode register carries the same mask plus the interrupt setting.
static void testMode() {
	static const u32 modes[] = {0, 1, 2, 3};

	printf("GIF_MODE:\n");
	for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
		*R_EE_GIF_MODE = modes[i];
		char label[32];
		sprintf(label, "mode %u", modes[i]);
		printStatus(label);
	}
	*R_EE_GIF_MODE = 0;
}

// Path one and path three both wanting the unit, with path one arriving while
// a controller transfer is running.
static void testContention() {
	printf("Path 1 while path 3 runs:\n");

	// A packet long enough that the kick lands mid transfer.
	GIF::Packet packet(4096);
	GIF::Tag tag;
	tag.SetLoops(64);
	tag.SetFormat(GIF::FORMAT_PACKED);
	tag.SetRegDescs(GIF::REG_NOP);
	packet.WriteTag(tag);
	for (int i = 0; i < 64; ++i) {
		packet.Emit(0);
		packet.Emit(0);
	}
	GIF::WriteProbe(packet);

	*GS::SIGLBLID = GIF::noLabel;
	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D2->madr = packet.Raw();
	DMA::D2->qwc = packet.Size() / 16;
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR | DMA::CHCR_DIR);
	printStatus("just after starting");

	int spins = 1000000;
	while (--spins > 0 && DMA::D2->chcr.Ongoing()) {
		continue;
	}
	quiet();
	printStatus("after finishing");
	printf("  label %016llx\n", *GS::SIGLBLID);
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
	*GS::CSR = GS::CSR_SIGNAL;
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testPathThree();
	testPathTwo();
	testMaskPathThree();
	testMode();
	testContention();

	printf("-- TEST END\n");
	return 0;
}
