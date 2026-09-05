#include <common-ee.h>
#include <ee_regs.h>
#include <kernel.h>
#include <string.h>
#include "../dmaregs.h"
#include "../dmasend.h"
#include "../../gs/emit_giftag.h"
#include "../../gs/gsregs.h"
#include "emit_vifcode.h"
#include "vifunits.h"

// The two codes that hand a packet straight through to the graphics interface
// rather than unpacking it.  A label write says whether the packet arrived,
// and the count field says how much of it the unit expected.

static const u64 noLabel = 0x3333333333333333ULL;

static void quiet() {
	for (volatile int i = 0; i < 20000; ++i) {
		continue;
	}
}

static u32 readLabel() {
	quiet();
	const u64 identifiers = *GS::SIGLBLID;
	*GS::CSR = GS::CSR_SIGNAL;
	return (u32)(identifiers >> 32);
}

// A packet that writes the label register, wrapped in a direct code.
static void sendDirect(bool highOnly, u32 declaredSize, u32 actualQuadwords,
                       u32 value) {
	VIF::Unit1.regs->fbrst = VIF::FBRST_RST;
	*GS::SIGLBLID = noLabel;

	GIF::Tag tag;
	tag.SetLoops(1);
	tag.SetEop();
	tag.SetFormat(GIF::FORMAT_PACKED);
	tag.SetRegDescs(GIF::REG_AD);

	VIF::Packet packet(512);
	if (highOnly) {
		packet.DIRECTHL(declaredSize);
	} else {
		packet.DIRECT(declaredSize);
	}
	packet.Data32((u32)tag.tag_.raw.v0);
	packet.Data32((u32)(tag.tag_.raw.v0 >> 32));
	packet.Data32((u32)tag.tag_.raw.v1);
	packet.Data32((u32)(tag.tag_.raw.v1 >> 32));
	packet.Data32((u32)GS::LABEL(value, 0xFFFFFFFF));
	packet.Data32((u32)(GS::LABEL(value, 0xFFFFFFFF) >> 32));
	packet.Data32(GS::REG_LABEL);
	packet.Data32(0);
	for (u32 i = 2; i < actualQuadwords; ++i) {
		packet.Data32(0);
		packet.Data32(0);
		packet.Data32(0);
		packet.Data32(0);
	}
	packet.NOP();
	packet.NOP();
	packet.NOP();

	const u32 bytes = (4 + actualQuadwords * 16 + 12 + 15) & ~15u;
	DMA::SendSimple(DMA::D1, packet.Raw(), bytes);
}

static void printOutcome(const char *what, u32 label) {
	printf("  %-26s label %04x, vif stat %08x, gif stat %08x\n", what, label,
	       VIF::Unit1.regs->stat.bits_, *R_EE_GIF_STAT);
}

// The declared size matching what follows, which is the ordinary case.
static void testMatched() {
	printf("DIRECT with a matching count:\n");
	for (u32 size = 2; size <= 4; ++size) {
		sendDirect(false, size, size, 0x1000 + size);
		char label[40];
		sprintf(label, "%u quadwords", size);
		printOutcome(label, readLabel());
	}
}

// The high only form, which is documented as taking the upper half of each
// quadword.
static void testHighOnly() {
	printf("DIRECTHL with a matching count:\n");
	for (u32 size = 2; size <= 4; ++size) {
		sendDirect(true, size, size, 0x2000 + size);
		char label[40];
		sprintf(label, "%u quadwords", size);
		printOutcome(label, readLabel());
	}
}

// A count of zero, which the sixteen bit field spells the same as 65536.
static void testZeroCount() {
	printf("A declared count of zero:\n");
	VIF::Unit1.regs->fbrst = VIF::FBRST_RST;
	*GS::SIGLBLID = noLabel;

	GIF::Tag tag;
	tag.SetLoops(1);
	tag.SetEop();
	tag.SetFormat(GIF::FORMAT_PACKED);
	tag.SetRegDescs(GIF::REG_AD);

	VIF::Packet packet(512);
	// The helper refuses zero, so the code goes in by hand.
	packet.Data32(0x50u << 24);
	packet.Data32((u32)tag.tag_.raw.v0);
	packet.Data32((u32)(tag.tag_.raw.v0 >> 32));
	packet.Data32((u32)tag.tag_.raw.v1);
	packet.Data32((u32)(tag.tag_.raw.v1 >> 32));
	packet.Data32((u32)GS::LABEL(0x3001, 0xFFFFFFFF));
	packet.Data32((u32)(GS::LABEL(0x3001, 0xFFFFFFFF) >> 32));
	packet.Data32(GS::REG_LABEL);
	packet.Data32(0);
	packet.NOP();
	packet.NOP();
	packet.NOP();
	DMA::SendSimple(DMA::D1, packet.Raw(), 48);

	printOutcome("count zero", readLabel());
	VIF::Unit1.regs->fbrst = VIF::FBRST_RST;
}

// A code that follows the data, so a miscount shows as the marker not landing.
static void testFollowingCode() {
	printf("A MARK after the data:\n");
	VIF::Unit1.regs->fbrst = VIF::FBRST_RST;
	VIF::Unit1.regs->mrk = 0;
	*GS::SIGLBLID = noLabel;

	GIF::Tag tag;
	tag.SetLoops(1);
	tag.SetEop();
	tag.SetFormat(GIF::FORMAT_PACKED);
	tag.SetRegDescs(GIF::REG_AD);

	VIF::Packet packet(512);
	packet.DIRECT(2);
	packet.Data32((u32)tag.tag_.raw.v0);
	packet.Data32((u32)(tag.tag_.raw.v0 >> 32));
	packet.Data32((u32)tag.tag_.raw.v1);
	packet.Data32((u32)(tag.tag_.raw.v1 >> 32));
	packet.Data32((u32)GS::LABEL(0x4001, 0xFFFFFFFF));
	packet.Data32((u32)(GS::LABEL(0x4001, 0xFFFFFFFF) >> 32));
	packet.Data32(GS::REG_LABEL);
	packet.Data32(0);
	packet.MARK(0x4444);
	packet.NOP();
	packet.NOP();
	packet.NOP();
	DMA::SendSimple(DMA::D1, packet.Raw(), 64);

	const u32 label = readLabel();
	printf("  label %04x, mark %04x\n", label, VIF::Unit1.regs->mrk & 0xFFFF);
}

// The first unit has no path to the graphics interface, so the same code there
// is the question of what it does instead.
static void testOnVif0() {
	printf("DIRECT sent to the first unit:\n");
	VIF::Unit0.regs->fbrst = VIF::FBRST_RST;
	VIF::Unit0.regs->err = (VIF::RegERRBits)0;

	VIF::Packet packet(256);
	packet.DIRECT(1);
	packet.Data32(0);
	packet.Data32(0);
	packet.Data32(0);
	packet.Data32(0);
	packet.MARK(0x5555);
	packet.NOP();
	packet.NOP();
	DMA::SendSimple(DMA::D0, packet.Raw(), 32);

	printf("  stat %08x err %08x mark %04x\n", VIF::Unit0.regs->stat.bits_,
	       VIF::Unit0.regs->err.bits_, VIF::Unit0.regs->mrk & 0xFFFF);
	VIF::Unit0.regs->fbrst = VIF::FBRST_RST;
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testMatched();
	testHighOnly();
	testZeroCount();
	testFollowingCode();
	testOnVif0();

	printf("-- TEST END\n");
	return 0;
}
