#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "../dmaregs.h"

// The ring the controller can route a transfer through.  What matters is the
// address arithmetic: the ring base has to be aligned to the ring size, and an
// address that runs off the end comes back to the base rather than carrying.

static u32 *ring = 0;
static const u32 ringBytes = 4 * 1024;

static u32 *source = 0;
static const u32 sourceBytes = 1024;

static void allocate() {
	// The base has to be a multiple of the size for the wrap to be a mask.
	ring = (u32 *)memalign(ringBytes, ringBytes);
	source = (u32 *)memalign(64, sourceBytes);
	for (u32 i = 0; i < sourceBytes / 4; ++i) {
		source[i] = 0xF1F00000 + i;
	}
	memset(ring, 0, ringBytes);
	SyncDCache(source, (u8 *)source + sourceBytes);
	SyncDCache(ring, (u8 *)ring + ringBytes);
}

static void testRingRegisters() {
	static const u32 sizes[] = {0x00000000, 0x000000FF, 0x00000FFF, 0x0000FFFF,
	                            0x7FFFFFFF, 0xFFFFFFFF};

	printf("The ring registers:\n");

	const u32 savedSize = *DMA::D_RBSR;
	void *const savedBase = *DMA::D_RBOR;
	printf("  they start at base %08x size %08x\n", (u32)savedBase, savedSize);

	for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		*DMA::D_RBSR = sizes[i];
		printf("  size wrote %08x, read %08x\n", sizes[i], *DMA::D_RBSR);
	}

	static const u32 bases[] = {0x00000000, 0xFFFFFFFF, 0x01FFFFF0, 0x0000000F};
	for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
		*DMA::D_RBOR = (void *)bases[i];
		printf("  base wrote %08x, read %08x\n", bases[i], (u32)*DMA::D_RBOR);
	}

	*DMA::D_RBSR = savedSize;
	*DMA::D_RBOR = savedBase;
}

// The address the scratchpad channel starts from, which is a scratchpad offset
// rather than a main memory address.
static void testScratchpadAddress() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x00003FF0, 0x00004000,
	                             0x0000000F};

	printf("The scratchpad address on each channel that has one:\n");
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		DMA::D8->sadr = values[i];
		DMA::D9->sadr = values[i];
		printf("  wrote %08x, fromSPR %08x toSPR %08x\n", values[i],
		       DMA::D8->sadr, DMA::D9->sadr);
	}
	DMA::D8->sadr = 0;
	DMA::D9->sadr = 0;
}

// Which drain channel the control register will accept.
static void testDrainSelection() {
	printf("The drain selection in D_CTRL:\n");

	const u32 saved = DMA::D_CTRL->bits_;
	for (u32 selection = 0; selection < 4; ++selection) {
		const u32 value = (saved & ~0x0000000Cu) | (selection << 2);
		*DMA::D_CTRL = (DMA::RegCTRLBits)value;
		printf("  wrote %d, D_CTRL reads %08x\n", selection, DMA::D_CTRL->bits_);
	}
	*DMA::D_CTRL = (DMA::RegCTRLBits)saved;
}

// A transfer into the ring with no drain running, which fills it and then has
// nowhere to go.  The count and address it stops at are what the ring costs.
static void testFillWithNoDrain() {
	printf("Filling the ring with nothing draining it:\n");

	const u32 savedCtrl = DMA::D_CTRL->bits_;
	const u32 savedSize = *DMA::D_RBSR;
	void *const savedBase = *DMA::D_RBOR;

	*DMA::D_RBOR = ring;
	*DMA::D_RBSR = ringBytes - 16;

	// Put the source in the scratchpad, since the channel that fills a ring is
	// the one that reads from there.
	volatile u32 *scratch = (volatile u32 *)0x70000000;
	for (u32 i = 0; i < sourceBytes / 4; ++i) {
		scratch[i] = 0xF1F00000 + i;
	}

	// The drain is set to the graphics path, which is not started, so the ring
	// fills and the transfer waits.
	*DMA::D_CTRL = (DMA::RegCTRLBits)((savedCtrl & ~0x0000000Cu) | (3 << 2) |
	                                  DMA::D_CTRL_DMAE);

	DMA::D8->sadr = 0;
	DMA::D8->madr = ring;
	DMA::D8->qwc = sourceBytes / 16;
	DMA::D8->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);

	u32 spins = 0;
	while ((DMA::D8->chcr & DMA::CHCR_STR) != 0 && spins < 100000) {
		spins++;
	}
	const u32 finished = (DMA::D8->chcr & DMA::CHCR_STR) == 0;
	printf("  finished %s after %d checks, qwc %d, madr %08x\n",
	       finished ? "yes" : "no ", spins, DMA::D8->qwc, (u32)DMA::D8->madr);

	DMA::D8->chcr = (DMA::ChannelRegCHCRBits)0;
	printf("  status %08x\n", DMA::D_STAT->bits_ & 0x0000FFFF);
	*DMA::D_STAT = (DMA::RegSTATBits)0x0000FFFF;

	*DMA::D_RBSR = savedSize;
	*DMA::D_RBOR = savedBase;
	*DMA::D_CTRL = (DMA::RegCTRLBits)savedCtrl;

	SyncDCache(ring, (u8 *)ring + ringBytes);
	printf("  the ring holds %08x %08x %08x %08x\n", ring[0], ring[1], ring[2],
	       ring[3]);
}

// The address a transfer lands on when the ring is smaller than the transfer,
// taken from what the hardware left in MADR rather than computed here.
static void testWrapAddress() {
	printf("Where a transfer larger than the ring stops:\n");

	const u32 savedCtrl = DMA::D_CTRL->bits_;
	const u32 savedSize = *DMA::D_RBSR;
	void *const savedBase = *DMA::D_RBOR;

	static const u32 ringSizes[] = {0x000000F0, 0x000001F0, 0x000003F0};
	for (unsigned i = 0; i < sizeof(ringSizes) / sizeof(ringSizes[0]); ++i) {
		*DMA::D_RBOR = ring;
		*DMA::D_RBSR = ringSizes[i];
		*DMA::D_CTRL = (DMA::RegCTRLBits)((savedCtrl & ~0x0000000Cu) | (3 << 2) |
		                                  DMA::D_CTRL_DMAE);

		DMA::D8->sadr = 0;
		DMA::D8->madr = (void *)((u32)ring + ringSizes[i]);
		DMA::D8->qwc = 4;
		DMA::D8->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);

		u32 spins = 0;
		while ((DMA::D8->chcr & DMA::CHCR_STR) != 0 && spins < 20000) {
			spins++;
		}
		printf("  size %08x, started at the last entry: qwc %d, madr offset %08x\n",
		       ringSizes[i], DMA::D8->qwc, (u32)DMA::D8->madr - (u32)ring);
		DMA::D8->chcr = (DMA::ChannelRegCHCRBits)0;
		*DMA::D_STAT = (DMA::RegSTATBits)0x0000FFFF;
	}

	*DMA::D_RBSR = savedSize;
	*DMA::D_RBOR = savedBase;
	*DMA::D_CTRL = (DMA::RegCTRLBits)savedCtrl;
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	allocate();
	testRingRegisters();
	testScratchpadAddress();
	testDrainSelection();
	testFillWithNoDrain();
	testWrapAddress();

	printf("-- TEST END\n");
	return 0;
}
