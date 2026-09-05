#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "../dmaregs.h"

// The three failure bits in the status register and the masks that go with
// them.  Two of the three have a mask and the third does not, which is the
// asymmetry a program that clears the whole high half runs into.

// The masks for the channels the link this output travels over uses, which are
// left alone throughout.
static const u32 linkMasks = 0x00E00000;

static void writeStat(u32 value) {
	*DMA::D_STAT = (DMA::RegSTATBits)(value & ~linkMasks);
}

static void printStat(const char *what) {
	const u32 bits = DMA::D_STAT->bits_;
	printf("  %-34s %08x  stall %d empty %d bus %d\n", what, bits,
	       (bits >> 13) & 1, (bits >> 14) & 1, (bits >> 15) & 1);
}

// Which of the high bits hold a one, taken by flipping each on its own.
static void testWhichMasksExist() {
	printf("Each mask bit taken on its own:\n");

	writeStat(0x0000FFFF);
	const u32 before = DMA::D_STAT->bits_ & 0xFFFF0000;
	writeStat(before);

	for (int bit = 16; bit < 32; ++bit) {
		const u32 value = 1u << bit;
		if ((value & linkMasks) != 0) {
			printf("  bit %2d: left alone, the link uses it\n", bit);
			continue;
		}
		writeStat(value);
		const u32 after = DMA::D_STAT->bits_ & 0xFFFF0000;
		printf("  bit %2d: %s\n", bit, after != 0 ? "held" : "dropped");
		writeStat(after);
	}
}

// The failure bits themselves, which cannot be set by writing.
static void testFailureBits() {
	printf("The failure bits:\n");

	printStat("as the program found them");

	writeStat(DMA::D_STAT_SIS | DMA::D_STAT_MEIS | DMA::D_STAT_BEIS);
	printStat("after clearing all three");

	// A one in the low half clears, so this cannot put any of them back.
	writeStat(DMA::D_STAT_SIS | DMA::D_STAT_MEIS | DMA::D_STAT_BEIS);
	printStat("after the same write again");
}

// The interrupt line the controller drives, which is the OR of each status bit
// with its mask.  The bus error has no mask, so it reaches the line whatever
// the high half says.
static void testInterruptLine() {
	static const int dmacCause = 1;

	printf("The controller's interrupt line:\n");

	writeStat(0x0000FFFF);
	const u32 masks = DMA::D_STAT->bits_ & 0xFFFF0000;
	writeStat(masks);

	volatile u32 *const intcStat = (volatile u32 *)0x1000F000;
	*intcStat = 1u << dmacCause;
	printf("  with every mask off, the cause reads %d\n",
	       (*intcStat >> dmacCause) & 1);

	// The transfer runs with its mask off.  Turning one on with nothing
	// registered to service the interrupt leaves the kernel taking it forever,
	// so the gating is recorded from the side that does not storm.
	static u32 *source = 0;
	if (source == 0) {
		source = (u32 *)memalign(64, 1024);
	}
	for (int i = 0; i < 256; ++i) {
		source[i] = 0xE1A00000 + i;
	}
	SyncDCache(source, (u8 *)source + 1024);

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D9->sadr = 0;
	DMA::D9->madr = source;
	DMA::D9->qwc = 1024 / 16;
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);
	while ((DMA::D9->chcr & DMA::CHCR_STR) != 0) {
		continue;
	}

	printf("  after an unmasked transfer, the cause reads %d, status %08x\n",
	       (*intcStat >> dmacCause) & 1, DMA::D_STAT->bits_);

	writeStat(0x0000FFFF);
	*intcStat = 1u << dmacCause;
	writeStat((DMA::D_STAT->bits_ ^ masks) & 0xFFFF0000);
	printf("  cleared, the cause reads %d, status %08x\n",
	       (*intcStat >> dmacCause) & 1, DMA::D_STAT->bits_);
}

// A channel started with a length no transfer can satisfy, which is the
// cheapest way to reach the failure path without a bad address.
static void testUnreachableLength() {
	printf("A transfer with a length past the end of memory:\n");

	writeStat(0x0000FFFF);
	*DMA::D_CTRL = DMA::D_CTRL_DMAE;

	DMA::D9->sadr = 0;
	DMA::D9->madr = (void *)0x01FFFF00;
	DMA::D9->qwc = 0xFFFF;
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);

	u32 spins = 0;
	while ((DMA::D9->chcr & DMA::CHCR_STR) != 0 && spins < 200000) {
		spins++;
	}
	printf("  finished %s after %d checks, qwc %d, madr %08x\n",
	       (DMA::D9->chcr & DMA::CHCR_STR) == 0 ? "yes" : "no ", spins,
	       DMA::D9->qwc, (u32)DMA::D9->madr);
	printStat("status after");

	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)0;
	writeStat(0x0000FFFF);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testFailureBits();
	testWhichMasksExist();
	testInterruptLine();
	testUnreachableLength();

	printf("-- TEST END\n");
	return 0;
}
