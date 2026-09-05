#include <common-ee.h>
#include <ee_regs.h>
#include "../gs/emit_giftag.h"
#include "../gs/gsregs.h"
#include "../dma/dmaregs.h"
#include "../dma/dmasend.h"
#include "gifprobe.h"

// Packed mode, where each register descriptor takes one quadword whatever it
// names.  Two registers can be read back, so the descriptor that writes them is
// what the field extraction is measured through; the rest are measured by how
// much of the packet they consume.

static void printLabel(const char *what, u64 label) {
	printf("  %-34s SIGLBLID %016llx%s\n", what, label,
	       GIF::ProbeReached(label) ? " (probe reached)" : "");
}

static const GIF::RegisterDescriptor descriptors[] = {
	GIF::REG_PRIM, GIF::REG_RGBAQ, GIF::REG_ST, GIF::REG_UV,
	GIF::REG_XYZF2, GIF::REG_XYZ2, GIF::REG_TEX0_1, GIF::REG_TEX0_2,
	GIF::REG_CLAMP_1, GIF::REG_CLAMP_2, GIF::REG_FOG,
	(GIF::RegisterDescriptor)0xB,
	GIF::REG_XYZF3, GIF::REG_XYZ3, GIF::REG_AD, GIF::REG_NOP,
};
static const char *const descriptorNames[] = {
	"PRIM", "RGBAQ", "ST", "UV", "XYZF2", "XYZ2", "TEX0_1", "TEX0_2",
	"CLAMP_1", "CLAMP_2", "FOG", "reserved", "XYZF3", "XYZ3", "A+D", "NOP",
};
static const int descriptorCount =
	sizeof(descriptors) / sizeof(descriptors[0]);

// One quadword per descriptor, whichever it is.  A descriptor that consumed a
// different amount would leave the probe unreached or run it early.
static void testDescriptorConsumption() {
	printf("One of each descriptor, then the probe:\n");
	for (int i = 0; i < descriptorCount; ++i) {
		GIF::ResetUnit();

		GIF::Packet packet(2048);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(descriptors[i]);
		packet.WriteTag(tag);
		packet.Emit(0);
		packet.Emit(0);
		GIF::WriteProbe(packet);

		printLabel(descriptorNames[i], GIF::SendAndReadLabel(packet));
	}
}

// The same descriptor repeated across loops, so the count is the product.
static void testLoopsAndRegisters() {
	printf("NOP descriptors across loops:\n");
	for (u32 regs = 1; regs <= 4; ++regs) {
		for (u32 loops = 1; loops <= 3; ++loops) {
			GIF::ResetUnit();

			GIF::Packet packet(4096);
			GIF::Tag tag;
			tag.SetLoops(loops);
			tag.SetFormat(GIF::FORMAT_PACKED);
			tag.SetRegDescs(
				GIF::REG_NOP,
				regs > 1 ? GIF::REG_NOP : GIF::REG_INVALID,
				regs > 2 ? GIF::REG_NOP : GIF::REG_INVALID,
				regs > 3 ? GIF::REG_NOP : GIF::REG_INVALID);
			packet.WriteTag(tag);
			for (u32 i = 0; i < regs * loops * 2; ++i) {
				packet.Emit(0);
			}
			GIF::WriteProbe(packet);

			char name[48];
			sprintf(name, "%u regs x %u loops = %u qw", regs, loops, regs * loops);
			printLabel(name, GIF::SendAndReadLabel(packet));
		}
	}
}

// A tag naming all sixteen descriptor slots, and one naming none.
static void testRegisterCountEdges() {
	printf("The ends of the descriptor count:\n");

	GIF::ResetUnit();
	{
		GIF::Packet packet(4096);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(GIF::REG_NOP, GIF::REG_NOP, GIF::REG_NOP, GIF::REG_NOP,
		                GIF::REG_NOP, GIF::REG_NOP, GIF::REG_NOP, GIF::REG_NOP,
		                GIF::REG_NOP, GIF::REG_NOP, GIF::REG_NOP, GIF::REG_NOP,
		                GIF::REG_NOP, GIF::REG_NOP, GIF::REG_NOP, GIF::REG_NOP);
		packet.WriteTag(tag);
		for (int i = 0; i < 32; ++i) {
			packet.Emit(0);
		}
		GIF::WriteProbe(packet);
		printLabel("sixteen descriptors", GIF::SendAndReadLabel(packet));
	}

	GIF::ResetUnit();
	{
		// NREGS of zero means sixteen, so this consumes as much as the case
		// above with the same data behind it.
		GIF::Packet packet(4096);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(GIF::REG_NOP);
		packet.WriteTag(tag);
		u64 *const raw = (u64 *)packet.Raw();
		raw[1] &= ~(0xFULL << (60 - 32));
		for (int i = 0; i < 32; ++i) {
			packet.Emit(0);
		}
		GIF::WriteProbe(packet);
		printLabel("NREGS written as zero", GIF::SendAndReadLabel(packet));
	}
}

// The one descriptor whose effect can be read back, which is where the field
// extraction is measured.
static void testLabelMasks() {
	static const u32 masks[] = {0xFFFFFFFF, 0x00000000, 0x0000FFFF, 0xFFFF0000,
	                            0x000000FF, 0xF0F0F0F0, 0x00000001, 0x80000000};

	printf("LABEL through A+D, with each mask:\n");
	for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
		GIF::ResetUnit();
		*GS::SIGLBLID = 0x1111111122222222ULL;

		GIF::Packet packet(2048);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetEop();
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(GIF::REG_AD);
		packet.WriteTag(tag);
		packet.Emit(GS::LABEL(0xAAAABBBB, masks[i]));
		packet.Emit(GS::REG_LABEL);

		DMA::SendSimple(DMA::D2, packet.Raw(), packet.Size());
		printf("  mask %08x: SIGLBLID %016llx\n", masks[i], *GS::SIGLBLID);
	}
	*GS::CSR = GS::CSR_SIGNAL;
}

// The other readable one, which also raises a bit in the status register and
// holds the next packet until that bit is cleared.
static void testSignalMasks() {
	static const u32 masks[] = {0xFFFFFFFF, 0x00000000, 0x0000FFFF};

	printf("SIGNAL through A+D, with each mask:\n");
	for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
		GIF::ResetUnit();
		*GS::CSR = GS::CSR_SIGNAL;
		*GS::SIGLBLID = 0x3333333344444444ULL;

		GIF::Packet packet(2048);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetEop();
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(GIF::REG_AD);
		packet.WriteTag(tag);
		packet.Emit(GS::SIGNAL(0xCCCCDDDD, masks[i]));
		packet.Emit(GS::REG_SIGNAL);

		DMA::SendSimple(DMA::D2, packet.Raw(), packet.Size());
		printf("  mask %08x: SIGLBLID %016llx, CSR signal %d\n", masks[i],
		       *GS::SIGLBLID, (u32)(*GS::CSR & GS::CSR_SIGNAL));
		*GS::CSR = GS::CSR_SIGNAL;
	}
}

// The address half of an A+D quadword, taken past the registers that exist.
static void testAddressField() {
	static const u64 addresses[] = {0x62, 0x63, 0x7F, 0x80, 0xFF, 0x100,
	                                0xFFFFFFFFFFFFFFFFULL};

	printf("A+D naming an address, then the probe:\n");
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		GIF::ResetUnit();

		GIF::Packet packet(2048);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(GIF::REG_AD);
		packet.WriteTag(tag);
		packet.Emit(0);
		packet.Emit(addresses[i]);
		GIF::WriteProbe(packet);

		char name[48];
		sprintf(name, "address %016llx", addresses[i]);
		printLabel(name, GIF::SendAndReadLabel(packet));
	}
}

// A+D naming LABEL from inside a packet with other descriptors around it, so
// the write lands only if the descriptors before it consumed what they should.
static void testAddressAfterOthers() {
	printf("LABEL written after each other descriptor:\n");
	for (int i = 0; i < descriptorCount; ++i) {
		GIF::ResetUnit();
		*GS::SIGLBLID = 0x5555555566666666ULL;

		GIF::Packet packet(2048);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetEop();
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(descriptors[i], GIF::REG_AD);
		packet.WriteTag(tag);
		packet.Emit(0);
		packet.Emit(0);
		packet.Emit(GS::LABEL(0x0000ABCD, 0xFFFFFFFF));
		packet.Emit(GS::REG_LABEL);

		DMA::SendSimple(DMA::D2, packet.Raw(), packet.Size());
		printf("  after %-9s SIGLBLID %016llx\n", descriptorNames[i],
		       *GS::SIGLBLID);
		*GS::CSR = GS::CSR_SIGNAL;
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testDescriptorConsumption();
	testLoopsAndRegisters();
	testRegisterCountEdges();
	testLabelMasks();
	testSignalMasks();
	testAddressField();
	testAddressAfterOthers();

	printf("-- TEST END\n");
	return 0;
}
