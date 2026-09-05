#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "../dmaregs.h"
#include "../dmasend.h"

// The controller's own registers rather than any transfer: which bits each
// channel keeps, what a zero length transfer does, what the enable register
// stops, and what a read sees while a transfer is under way.

struct Named {
	volatile DMA::Channel *channel;
	const char *name;
};

static const Named channels[] = {
	{DMA::D0, "VIF0"}, {DMA::D1, "VIF1"}, {DMA::D2, "GIF"},
	{DMA::D8, "fromSPR"}, {DMA::D9, "toSPR"},
};

static const int channelCount = sizeof(channels) / sizeof(channels[0]);

static u32 *source = 0;
static const u32 sourceBytes = 4 * 1024;

static void fillSource() {
	for (u32 i = 0; i < sourceBytes / 4; ++i) {
		source[i] = 0xD1A00000 + i;
	}
	SyncDCache(source, (u8 *)source + sourceBytes);
}

// Which address bits a channel keeps, with nothing running.
static void testAddressBits() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x0000000F, 0x80000000,
	                             0x01FFFFF0};

	printf("MADR, TADR and SADR readback:\n");
	for (int c = 0; c < channelCount; ++c) {
		volatile DMA::Channel *channel = channels[c].channel;
		channel->chcr = (DMA::ChannelRegCHCRBits)0;

		printf("  %-8s madr:", channels[c].name);
		for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			channel->madr = (void *)values[v];
			printf(" %08x", (u32)channel->madr);
		}
		printf("\n  %-8s tadr:", channels[c].name);
		for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			channel->tadr = (void *)values[v];
			printf(" %08x", (u32)channel->tadr);
		}
		printf("\n  %-8s sadr:", channels[c].name);
		for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			channel->sadr = values[v];
			printf(" %08x", channel->sadr);
		}
		printf("\n");
		channel->madr = 0;
		channel->tadr = 0;
		channel->sadr = 0;
	}
}

// The count register is documented as sixteen bits.
static void testCountBits() {
	static const u32 values[] = {0x00000000, 0x0000FFFF, 0x00010000, 0xFFFFFFFF};

	printf("QWC readback:\n");
	for (int c = 0; c < channelCount; ++c) {
		volatile DMA::Channel *channel = channels[c].channel;
		channel->chcr = (DMA::ChannelRegCHCRBits)0;

		printf("  %-8s:", channels[c].name);
		for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			channel->qwc = values[v];
			printf(" %08x", channel->qwc);
		}
		printf("\n");
		channel->qwc = 0;
	}
}

// The control register carries the mode, the direction and the tag bits, and
// the start bit is the one the hardware clears itself.
static void testControlBits() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFE, 0x000000FE, 0xFFFF0000};

	printf("CHCR readback, start bit left alone:\n");
	// Bit eight is the start bit, and a value that sets it runs a transfer with
	// whatever address and count the channel happens to be holding.
	for (int c = 0; c < channelCount; ++c) {
		volatile DMA::Channel *channel = channels[c].channel;

		printf("  %-8s:", channels[c].name);
		for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			channel->chcr = (DMA::ChannelRegCHCRBits)(values[v] & ~DMA::CHCR_STR);
			printf(" %08x", channel->chcr.bits_);
		}
		printf("\n");
		channel->chcr = (DMA::ChannelRegCHCRBits)0;
	}
}

// A transfer with nothing to move: whether it finishes and where it leaves the
// address.
static void testZeroLength() {
	printf("A transfer of zero quadwords:\n");
	volatile u32 *scratch = (volatile u32 *)0x70000000;
	for (int i = 0; i < 8; ++i) {
		scratch[i] = 0x5EED0000 + i;
	}

	fillSource();
	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D9->sadr = 0;
	DMA::D9->madr = source;
	DMA::D9->qwc = 0;
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);

	int spins = 1000000;
	while (--spins > 0 && DMA::D9->chcr.Ongoing()) {
		continue;
	}

	printf("  finished %s, madr %08x qwc %d chcr %08x\n", spins > 0 ? "yes" : "no",
	       (u32)DMA::D9->madr, DMA::D9->qwc, DMA::D9->chcr.bits_);
	printf("  scratchpad: %08x %08x %08x %08x\n", scratch[0], scratch[4],
	       scratch[8], scratch[12]);
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)0;
}

// The two enable registers hold every channel off at once.
static void testEnable() {
	volatile u32 *const enableRead = (volatile u32 *)0x1000F520;
	volatile u32 *const enableWrite = (volatile u32 *)0x1000F590;

	printf("The enable register:\n");
	const u32 initial = *enableRead;
	printf("  initial %08x\n", initial);

	*enableWrite = 0x00010000;
	printf("  after suspending: %08x\n", *enableRead);

	fillSource();
	volatile u32 *scratch = (volatile u32 *)0x70000000;
	for (int i = 0; i < 8; ++i) {
		scratch[i] = 0;
	}
	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D9->sadr = 0;
	DMA::D9->madr = source;
	DMA::D9->qwc = 1;
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);

	for (volatile int i = 0; i < 20000; ++i) {
		continue;
	}
	printf("  while suspended: ongoing %s, scratchpad %08x\n",
	       DMA::D9->chcr.Ongoing() ? "yes" : "no", scratch[0]);

	*enableWrite = 0x00000000;
	int spins = 1000000;
	while (--spins > 0 && DMA::D9->chcr.Ongoing()) {
		continue;
	}
	printf("  after resuming: ongoing %s, scratchpad %08x, enable %08x\n",
	       DMA::D9->chcr.Ongoing() ? "yes" : "no", scratch[0], *enableRead);
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)0;
	*enableWrite = initial;
}

// The priority control register, which decides the order two ready channels
// go in.
static void testPriority() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x000003FF, 0x80000000};

	printf("D_PCR readback:\n");
	const u32 saved = DMA::D_PCR->bits_;
	for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
		*DMA::D_PCR = (DMA::RegPCRBits)values[v];
		const u32 back = DMA::D_PCR->bits_;
		*DMA::D_PCR = (DMA::RegPCRBits)saved;
		printf("  wrote %08x, read %08x\n", values[v], back);
	}
}

// The main control register, whose cycle stealing and stall fields decide how
// a transfer shares the bus.
static void testControl() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x00000001, 0x0000FFFF};

	// The stall and ring fields reroute whatever is running, so the value is put
	// back before anything else uses a channel, printing included.
	printf("D_CTRL readback:\n");
	const u32 saved = DMA::D_CTRL->bits_;
	for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
		*DMA::D_CTRL = (DMA::RegCTRLBits)values[v];
		const u32 back = DMA::D_CTRL->bits_;
		*DMA::D_CTRL = (DMA::RegCTRLBits)saved;
		printf("  wrote %08x, read %08x\n", values[v], back);
	}
}

// What a read of the address and count sees part way through a long transfer.
static void testReadWhileRunning() {
	printf("Reading a channel mid transfer:\n");

	fillSource();
	volatile u32 *scratch = (volatile u32 *)0x70000000;
	for (int i = 0; i < 8; ++i) {
		scratch[i] = 0;
	}

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D9->sadr = 0;
	DMA::D9->madr = source;
	DMA::D9->qwc = sourceBytes / 16;
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);

	const u32 madrDuring = (u32)DMA::D9->madr;
	const u32 qwcDuring = DMA::D9->qwc;
	const u32 chcrDuring = DMA::D9->chcr.bits_;

	int spins = 1000000;
	while (--spins > 0 && DMA::D9->chcr.Ongoing()) {
		continue;
	}

	printf("  during: madr %08x qwc %d chcr %08x\n", madrDuring, qwcDuring,
	       chcrDuring);
	printf("  after:  madr %08x qwc %d chcr %08x, first word %08x\n",
	       (u32)DMA::D9->madr, DMA::D9->qwc, DMA::D9->chcr.bits_, scratch[0]);
	DMA::D9->chcr = (DMA::ChannelRegCHCRBits)0;
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	source = (u32 *)memalign(16, sourceBytes);

	testAddressBits();
	testCountBits();
	testControlBits();
	testZeroLength();
	testEnable();
	testPriority();
	testControl();
	testReadWhileRunning();

	free(source);

	printf("-- TEST END\n");
	return 0;
}
