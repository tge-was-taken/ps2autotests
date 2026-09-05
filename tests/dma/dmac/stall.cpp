#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "../dmaregs.h"

// The stall address and the two fields that decide which channel writes it and
// which channel waits on it.

static void testStallAddress() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x01FFFFF0, 0x0000000F,
	                             0x80000000};

	printf("The stall address:\n");
	void *const saved = *DMA::D_STADR;
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		*DMA::D_STADR = (void *)values[i];
		printf("  wrote %08x, read %08x\n", values[i], (u32)*DMA::D_STADR);
	}
	*DMA::D_STADR = saved;
}

// The two selections, taken one at a time so a value that lands in the wrong
// field shows up as a change in the other.
static void testSelections() {
	printf("The stall source and drain selections:\n");

	const u32 saved = DMA::D_CTRL->bits_;
	for (u32 source = 0; source < 4; ++source) {
		const u32 value = (saved & ~0x00000030u) | (source << 4);
		*DMA::D_CTRL = (DMA::RegCTRLBits)value;
		printf("  source %d: D_CTRL reads %08x\n", source, DMA::D_CTRL->bits_);
	}
	for (u32 drain = 0; drain < 4; ++drain) {
		const u32 value = (saved & ~0x000000C0u) | (drain << 6);
		*DMA::D_CTRL = (DMA::RegCTRLBits)value;
		printf("  drain  %d: D_CTRL reads %08x\n", drain, DMA::D_CTRL->bits_);
	}
	*DMA::D_CTRL = (DMA::RegCTRLBits)saved;
}

// A source transfer with a stall selection set, which writes the stall address
// as it goes.  Nothing is draining, so what is recorded is whether the address
// moved at all.
static void testSourceWritesStallAddress() {
	static u32 *source = 0;
	static u32 *destination = 0;
	if (source == 0) {
		source = (u32 *)memalign(64, 1024);
		destination = (u32 *)memalign(64, 1024);
	}
	for (int i = 0; i < 256; ++i) {
		source[i] = 0x57A00000 + i;
	}
	memset(destination, 0, 1024);
	SyncDCache(source, (u8 *)source + 1024);
	SyncDCache(destination, (u8 *)destination + 1024);

	printf("A transfer with the stall source set to it:\n");

	const u32 savedCtrl = DMA::D_CTRL->bits_;
	void *const savedStall = *DMA::D_STADR;

	*DMA::D_STADR = (void *)0;
	// Selection one is the scratchpad channel, which is the one this uses.
	*DMA::D_CTRL = (DMA::RegCTRLBits)((savedCtrl & ~0x00000030u) | (1 << 4) |
	                                  DMA::D_CTRL_DMAE);

	DMA::D8->sadr = 0;
	DMA::D8->madr = destination;
	DMA::D8->qwc = 1024 / 16;
	DMA::D8->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);

	u32 spins = 0;
	while ((DMA::D8->chcr & DMA::CHCR_STR) != 0 && spins < 100000) {
		spins++;
	}
	printf("  finished %s, stall address %08x, offset into the buffer %08x\n",
	       (DMA::D8->chcr & DMA::CHCR_STR) == 0 ? "yes" : "no ",
	       (u32)*DMA::D_STADR, (u32)*DMA::D_STADR - (u32)destination);

	DMA::D8->chcr = (DMA::ChannelRegCHCRBits)0;
	*DMA::D_STAT = (DMA::RegSTATBits)0x0000FFFF;
	*DMA::D_STADR = savedStall;
	*DMA::D_CTRL = (DMA::RegCTRLBits)savedCtrl;
}

// A drain transfer waiting on a stall address that never moves, which is the
// case a program hits when it gets the two selections the wrong way round.
static void testDrainWaitsForever() {
	static u32 *source = 0;
	if (source == 0) {
		source = (u32 *)memalign(64, 1024);
	}
	for (int i = 0; i < 256; ++i) {
		source[i] = 0x57B00000 + i;
	}
	SyncDCache(source, (u8 *)source + 1024);

	printf("A drain waiting on a stall address that stays put:\n");

	const u32 savedCtrl = DMA::D_CTRL->bits_;
	void *const savedStall = *DMA::D_STADR;

	*DMA::D_STADR = source;
	// Selection two is the scratchpad drain, which is the one this uses.
	*DMA::D_CTRL = (DMA::RegCTRLBits)((savedCtrl & ~0x000000C0u) | (2 << 6) |
	                                  DMA::D_CTRL_DMAE);

	DMA::D9->sadr = 0;
	DMA::D9->madr = source;
	DMA::D9->qwc = 1024 / 16;
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);

	u32 spins = 0;
	while ((DMA::D9->chcr & DMA::CHCR_STR) != 0 && spins < 100000) {
		spins++;
	}
	printf("  finished %s after %d checks, qwc %d, status %08x\n",
	       (DMA::D9->chcr & DMA::CHCR_STR) == 0 ? "yes" : "no ", spins,
	       DMA::D9->qwc, DMA::D_STAT->bits_ & 0x0000FFFF);

	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)0;
	*DMA::D_STAT = (DMA::RegSTATBits)0x0000FFFF;
	*DMA::D_STADR = savedStall;
	*DMA::D_CTRL = (DMA::RegCTRLBits)savedCtrl;
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testStallAddress();
	testSelections();
	testSourceWritesStallAddress();
	testDrainWaitsForever();

	printf("-- TEST END\n");
	return 0;
}
