#include <common-ee.h>
#include <ee_regs.h>
#include "../gs/emit_giftag.h"
#include "../gs/gsregs.h"
#include "gifprobe.h"

static void printLabel(const char *what, u64 label) {
	printf("  %s: SIGLBLID %016llx%s\n", what, label,
	       GIF::ProbeReached(label) ? " (probe reached)" : "");
}

// One packed tag with nothing but NOP registers, then the probe.  If the unit
// counts the data right, the probe is the next tag it reads.
static void testNloop() {
	printf("packed, one NOP register, varying NLOOP:\n");
	for (u32 loops = 0; loops <= 4; ++loops) {
		GIF::ResetUnit();

		GIF::Packet packet(1024);
		GIF::Tag tag;
		tag.SetLoops(loops);
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(GIF::REG_NOP);
		packet.WriteTag(tag);
		for (u32 i = 0; i < loops; ++i) {
			packet.Emit(0);
			packet.Emit(0);
		}
		GIF::WriteProbe(packet);

		char name[32];
		sprintf(name, "NLOOP %u", loops);
		printLabel(name, GIF::SendAndReadLabel(packet));
	}
}

// Sixteen registers encode as an NREG of zero, so this says whether the unit
// reads that as sixteen or as none.
static void testNregWrap() {
	static const u32 counts[] = {1, 2, 8, 15, 16};

	printf("packed, NLOOP 1, varying register count:\n");
	for (unsigned c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c) {
		const u32 count = counts[c];
		GIF::ResetUnit();

		GIF::Packet packet(1024);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(
			GIF::REG_NOP, count > 1 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 2 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 3 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 4 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 5 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 6 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 7 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 8 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 9 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 10 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 11 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 12 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 13 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 14 ? GIF::REG_NOP : GIF::REG_INVALID,
			count > 15 ? GIF::REG_NOP : GIF::REG_INVALID);
		packet.WriteTag(tag);
		for (u32 i = 0; i < count; ++i) {
			packet.Emit(0);
			packet.Emit(0);
		}
		GIF::WriteProbe(packet);

		char name[32];
		sprintf(name, "%2u registers", count);
		printLabel(name, GIF::SendAndReadLabel(packet));
	}
}

// A tag without EOP means another tag follows, so the probe should still run.
// A tag with EOP means the packet is over, and whether the probe runs anyway
// is the question.
static void testEop() {
	printf("EOP on the leading tag:\n");
	for (int eop = 0; eop < 2; ++eop) {
		GIF::ResetUnit();

		GIF::Packet packet(1024);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetEop(eop != 0);
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(GIF::REG_NOP);
		packet.WriteTag(tag);
		packet.Emit(0);
		packet.Emit(0);
		GIF::WriteProbe(packet);

		printLabel(eop != 0 ? "EOP set  " : "EOP clear", GIF::SendAndReadLabel(packet));
	}
}

// A+D takes its destination from the data, so this says which addresses the
// unit accepts and what it does with one that is out of range.
static void testAddressData() {
	static const u32 addresses[] = {
		GS::REG_LABEL, GS::REG_SIGNAL, 0x63, 0x7F, 0x80, 0xFF,
	};

	printf("A+D to each address, then the probe:\n");
	for (unsigned a = 0; a < sizeof(addresses) / sizeof(addresses[0]); ++a) {
		GIF::ResetUnit();

		GIF::Packet packet(1024);
		GIF::Tag tag;
		tag.SetLoops(1);
		tag.SetFormat(GIF::FORMAT_PACKED);
		tag.SetRegDescs(GIF::REG_AD);
		packet.WriteTag(tag);
		packet.Emit(GS::LABEL(0x5555, 0xFFFFFFFF));
		packet.Emit(addresses[a]);
		GIF::WriteProbe(packet);

		char name[32];
		sprintf(name, "address %02x", addresses[a]);
		printLabel(name, GIF::SendAndReadLabel(packet));
	}
}

// Two tags back to back, so a miscount on the first shows up as the second
// never running.
static void testChainedTags() {
	printf("two NOP tags then the probe:\n");
	for (u32 loops = 1; loops <= 3; ++loops) {
		GIF::ResetUnit();

		GIF::Packet packet(1024);
		for (int t = 0; t < 2; ++t) {
			GIF::Tag tag;
			tag.SetLoops(loops);
			tag.SetFormat(GIF::FORMAT_PACKED);
			tag.SetRegDescs(GIF::REG_NOP, GIF::REG_NOP);
			packet.WriteTag(tag);
			for (u32 i = 0; i < loops * 2; ++i) {
				packet.Emit(0);
				packet.Emit(0);
			}
		}
		GIF::WriteProbe(packet);

		char name[32];
		sprintf(name, "NLOOP %u x 2 regs", loops);
		printLabel(name, GIF::SendAndReadLabel(packet));
	}
}

static void testStatus() {
	GIF::ResetUnit();
	printf("GIF_STAT after a reset: %08x\n", *R_EE_GIF_STAT);

	GIF::Packet packet(256);
	GIF::WriteProbe(packet);
	GIF::SendAndReadLabel(packet);
	printf("GIF_STAT after a packet: %08x\n", *R_EE_GIF_STAT);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testNloop();
	testNregWrap();
	testEop();
	testAddressData();
	testChainedTags();
	testStatus();

	printf("-- TEST END\n");
	return 0;
}
