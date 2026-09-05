#include <common-ee.h>
#include <ee_regs.h>
#include "../gs/emit_giftag.h"
#include "../gs/gsregs.h"
#include "gifprobe.h"

static void printLabel(const char *what, u64 label) {
	printf("  %s: SIGLBLID %016llx%s\n", what, label,
	       GIF::ProbeReached(label) ? " (probe reached)" : "");
}

// A reglist packs two registers per quadword, so an odd count leaves a half
// quadword the unit has to account for.
static void testReglistCounts() {
	printf("reglist, NOP registers, then the probe:\n");
	for (u32 regs = 1; regs <= 4; ++regs) {
		for (u32 loops = 1; loops <= 3; ++loops) {
			GIF::ResetUnit();

			GIF::Packet packet(2048);
			GIF::Tag tag;
			tag.SetLoops(loops);
			tag.SetFormat(GIF::FORMAT_REGLIST);
			tag.SetRegDescs(
				GIF::REG_NOP,
				regs > 1 ? GIF::REG_NOP : GIF::REG_INVALID,
				regs > 2 ? GIF::REG_NOP : GIF::REG_INVALID,
				regs > 3 ? GIF::REG_NOP : GIF::REG_INVALID);
			packet.WriteTag(tag);

			const u32 fields = regs * loops;
			const u32 quadwords = (fields + 1) / 2;
			for (u32 i = 0; i < quadwords * 2; ++i) {
				packet.Emit(0);
			}
			GIF::WriteProbe(packet);

			char name[48];
			sprintf(name, "%u regs x %u loops = %u qw", regs, loops, quadwords);
			printLabel(name, GIF::SendAndReadLabel(packet));
		}
	}
}

// Image mode carries no register list, so NLOOP counts quadwords directly.
static void testImageCounts() {
	printf("image, then the probe:\n");
	for (u32 loops = 0; loops <= 4; ++loops) {
		GIF::ResetUnit();

		GIF::Packet packet(2048);
		GIF::Tag tag;
		tag.SetLoops(loops);
		tag.SetFormat(GIF::FORMAT_IMAGE);
		tag.SetRegDescs(GIF::REG_NOP);
		packet.WriteTag(tag);
		for (u32 i = 0; i < loops * 2; ++i) {
			packet.Emit(0);
		}
		GIF::WriteProbe(packet);

		char name[32];
		sprintf(name, "NLOOP %u", loops);
		printLabel(name, GIF::SendAndReadLabel(packet));
	}
}

// The fourth format value is documented as disabled, which should behave like
// image but drop the data.
static void testDisabledFormat() {
	printf("format 3, then the probe:\n");
	for (u32 loops = 1; loops <= 3; ++loops) {
		GIF::ResetUnit();

		GIF::Packet packet(2048);
		GIF::Tag tag;
		tag.SetLoops(loops);
		tag.SetFormat(GIF::FORMAT_IMAGE_3);
		tag.SetRegDescs(GIF::REG_NOP);
		packet.WriteTag(tag);
		for (u32 i = 0; i < loops * 2; ++i) {
			packet.Emit(0);
		}
		GIF::WriteProbe(packet);

		char name[32];
		sprintf(name, "NLOOP %u", loops);
		printLabel(name, GIF::SendAndReadLabel(packet));
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testReglistCounts();
	testImageCounts();
	testDisabledFormat();

	printf("-- TEST END\n");
	return 0;
}
