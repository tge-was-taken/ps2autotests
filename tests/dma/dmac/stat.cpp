#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "../dmaregs.h"

// The status register, whose two halves are written differently: a one in the
// low half clears the bit it lands on, and a one in the high half flips the
// mask bit it lands on.  Writing the whole register with ones is the case that
// separates the two.

static u32 savedStat;

// The masks for the channels the link this output travels over uses.  Setting
// one of those while its status bit is up storms, so every write below leaves
// them alone.
static const u32 linkMasks = 0x00E00000;

static void printStat(const char *what) {
	printf("  %-34s %08x\n", what, DMA::D_STAT->bits_);
}

static void writeStat(u32 value) {
	*DMA::D_STAT = (DMA::RegSTATBits)(value & ~linkMasks);
}

// A one in the low half clears, a zero leaves alone.
static void testClearing() {
	printf("Writing the low half:\n");

	writeStat(0x0000FFFF);
	printStat("after clearing every status bit");

	writeStat(0x00000000);
	printStat("after writing zero");

	writeStat(0x0000FFFF);
	printStat("after clearing again");
}

// A one in the high half flips, so writing the same value twice returns the
// register to where it started.
static void testToggling() {
	printf("Writing the high half:\n");

	const u32 before = DMA::D_STAT->bits_;
	writeStat(0x00010000);
	printStat("after a one on the first mask bit");
	writeStat(0x00010000);
	printStat("after the same write again");
	printf("  %-34s %s\n", "back where it started",
	       DMA::D_STAT->bits_ == before ? "yes" : "no");

	writeStat(0xFFFF0000);
	printStat("after a one on every free mask bit");
	writeStat(0xFFFF0000);
	printStat("after the same write again");
}

// Both halves at once, which clears and flips in the same write.
static void testBothHalves() {
	printf("Writing both halves:\n");

	writeStat(0x0000FFFF);
	const u32 masks = DMA::D_STAT->bits_ & 0xFFFF0000;
	writeStat(0xFFFFFFFF);
	printStat("after ones everywhere");
	printf("  %-34s %s\n", "the masks flipped",
	       (DMA::D_STAT->bits_ & 0xFFFF0000) == (~masks & 0xFFFF0000) ? "yes" : "no");
	writeStat(0xFFFF0000);
	printStat("after flipping them back");
}

// Which bits the register keeps at all.
static void testWhichBitsExist() {
	printf("Bits the register holds:\n");

	writeStat(0x0000FFFF);
	writeStat(DMA::D_STAT->bits_ & 0xFFFF0000);
	printStat("with every mask cleared");

	writeStat(0xFFFF0000);
	printStat("with every mask set");

	const u32 all = DMA::D_STAT->bits_;
	writeStat(0xFFFF0000);
	printf("  %-34s %08x\n", "mask bits that exist", all & 0xFFFF0000);
}

// A transfer that really does raise a status bit, so the clearing above is
// shown against a bit the hardware set rather than one this program did.
static void testRealInterrupt() {
	static u32 *source = 0;
	if (source == 0) {
		source = (u32 *)memalign(64, 1024);
	}
	for (int i = 0; i < 256; ++i) {
		source[i] = 0xD1A00000 + i;
	}
	SyncDCache(source, (u8 *)source + 1024);

	printf("A transfer that raises a status bit:\n");

	writeStat(0x0000FFFF);
	printStat("before the transfer");

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D9->sadr = 0;
	DMA::D9->madr = source;
	DMA::D9->qwc = 1024 / 16;
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);
	while ((DMA::D9->chcr & DMA::CHCR_STR) != 0) {
		continue;
	}

	printStat("after the transfer");
	printf("  %-34s %s\n", "the toSPR bit is set",
	       (DMA::D_STAT->bits_ & DMA::D_STAT_CIS9) != 0 ? "yes" : "no");

	writeStat(DMA::D_STAT_CIS9);
	printStat("after clearing it");

	writeStat(DMA::D_STAT_CIS9);
	printStat("after clearing it again");
}

// The two registers that read and write the halt bit, which are a pair rather
// than one register at two addresses.
static void testEnableRegisters() {
	printf("The halt registers:\n");

	const u32 saved = DMA::D_ENABLER->bits_;
	printf("  D_ENABLER reads %08x\n", saved);

	*DMA::D_ENABLEW = (DMA::RegENABLERWBits)0x00010000;
	printf("  after writing 00010000, D_ENABLER reads %08x\n",
	       DMA::D_ENABLER->bits_);

	// Only bit sixteen is defined.  Writing ones would halt every channel,
	// including the ones this program's output travels over.
	*DMA::D_ENABLEW = (DMA::RegENABLERWBits)saved;
	printf("  put back, D_ENABLER reads %08x\n", DMA::D_ENABLER->bits_);
}

// The skip and count register, which decides how a transfer interleaves.
static void testSkipCount() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x00FF00FF, 0x000000FF};

	printf("D_SQWC readback:\n");
	const u32 saved = DMA::D_SQWC->bits_;
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		*DMA::D_SQWC = values[i];
		printf("  wrote %08x, read %08x\n", values[i], DMA::D_SQWC->bits_);
	}
	*DMA::D_SQWC = saved;
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	savedStat = DMA::D_STAT->bits_;
	printf("D_STAT starts at %08x\n", savedStat);

	testClearing();
	testToggling();
	testBothHalves();
	testWhichBitsExist();
	testRealInterrupt();
	testEnableRegisters();
	testSkipCount();

	writeStat(0x0000FFFF);
	writeStat((DMA::D_STAT->bits_ ^ savedStat) & 0xFFFF0000);
	printf("D_STAT restored to %08x\n", DMA::D_STAT->bits_);

	printf("-- TEST END\n");
	return 0;
}
