#include <common-ee.h>
#include <iopheap.h>
#include <kernel.h>
#include <sifdma.h>
#include <string.h>

// Transfers to the other processor through the kernel's own queue.  The queue
// is shared with whatever is already talking over it, so every transfer here
// goes to memory this program asked the other side for and every wait is
// bounded.  Nothing is sent to an address the heap did not hand back: a
// transfer with a null destination lands on the other processor's kernel and it
// starts executing whatever was written there.

static u32 source[256] __attribute__((aligned(64)));
static u32 destination[256] __attribute__((aligned(64)));

static void *iopBuffer = 0;
static const int bufferBytes = 1024;

static void fillSource(u32 tag) {
	for (int i = 0; i < 256; ++i) {
		source[i] = (tag << 24) | i;
	}
	SyncDCache(source, source + 256);
}

// A transfer identifier of zero means the queue refused, so a bounded wait on
// one that was accepted is all that is needed.
static int waitFor(int identifier) {
	if (identifier == 0) {
		return -1;
	}
	for (int spins = 0; spins < 1000000; ++spins) {
		if (SifDmaStat(identifier) < 0) {
			return spins;
		}
	}
	return -2;
}

static void testHeap() {
	printf("The other processor's heap:\n");
	printf("  SifInitIopHeap %d\n", SifInitIopHeap());

	iopBuffer = SifAllocIopHeap(bufferBytes);
	printf("  SifAllocIopHeap(%d) gave %08x\n", bufferBytes, (u32)iopBuffer);

	void *const second = SifAllocIopHeap(bufferBytes);
	printf("  a second one gave %08x, %d apart\n", (u32)second,
	       (int)((u32)second - (u32)iopBuffer));
	printf("  freeing the second: %d\n", SifFreeIopHeap(second));

	printf("  a size of zero gives %08x\n", (u32)SifAllocIopHeap(0));
	printf("  a size of minus one gives %08x\n", (u32)SifAllocIopHeap(-1));
}

// One transfer out and one back, which is the whole path.
static void testRoundTrip() {
	if (iopBuffer == 0) {
		printf("No buffer on the other side, so no round trip\n");
		return;
	}

	printf("A round trip through the other processor's memory:\n");

	fillSource(0x5D);
	memset(destination, 0, sizeof(destination));
	SyncDCache(destination, destination + 256);

	SifDmaTransfer_t out;
	out.src = source;
	out.dest = iopBuffer;
	out.size = bufferBytes;
	out.attr = 0;
	const int outId = SifSetDma(&out, 1);
	printf("  out: identifier %d, finished after %d checks\n", outId,
	       waitFor(outId));

	SifDmaTransfer_t back;
	back.src = iopBuffer;
	back.dest = destination;
	back.size = bufferBytes;
	back.attr = 0;
	const int backId = SifSetDma(&back, 1);
	printf("  back: identifier %d, finished after %d checks\n", backId,
	       waitFor(backId));

	SyncDCache(destination, destination + 256);
	int same = 0;
	for (int i = 0; i < 256; ++i) {
		if (destination[i] == source[i]) {
			same++;
		}
	}
	printf("  %d of 256 words came back the same\n", same);
	printf("  first four %08x %08x %08x %08x\n", destination[0], destination[1],
	       destination[2], destination[3]);
}

// Addresses that are not on a quadword boundary, which the hardware cannot
// carry and the queue has to either fix or refuse.
static void testAlignment() {
	if (iopBuffer == 0) {
		return;
	}

	printf("Transfers off a quadword boundary:\n");
	for (int offset = 0; offset < 16; offset += 4) {
		SifDmaTransfer_t transfer;
		transfer.src = (u8 *)source + offset;
		transfer.dest = (u8 *)iopBuffer + offset;
		transfer.size = 64;
		transfer.attr = 0;

		const int identifier = SifSetDma(&transfer, 1);
		printf("  offset %2d: identifier %d, finished after %d checks\n", offset,
		       identifier, waitFor(identifier));
	}

	printf("Sizes that are not a multiple of sixteen:\n");
	static const int sizes[] = {1, 4, 15, 16, 17, 31, 33};
	for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		SifDmaTransfer_t transfer;
		transfer.src = source;
		transfer.dest = iopBuffer;
		transfer.size = sizes[i];
		transfer.attr = 0;

		const int identifier = SifSetDma(&transfer, 1);
		printf("  size %2d: identifier %d, finished after %d checks\n", sizes[i],
		       identifier, waitFor(identifier));
	}
}

// The attribute field, which asks for an interrupt at either end.
static void testAttributes() {
	if (iopBuffer == 0) {
		return;
	}

	static const int attributes[] = {0, SIF_DMA_INT_I, SIF_DMA_INT_O,
	                                 SIF_DMA_INT_I | SIF_DMA_INT_O, SIF_DMA_ERT,
	                                 0xFF};

	printf("Each attribute:\n");
	for (unsigned i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i) {
		SifDmaTransfer_t transfer;
		transfer.src = source;
		transfer.dest = iopBuffer;
		transfer.size = 256;
		transfer.attr = attributes[i];

		const int identifier = SifSetDma(&transfer, 1);
		printf("  %02x: identifier %d, finished after %d checks\n", attributes[i],
		       identifier, waitFor(identifier));
	}
}

// More than one transfer in a single call, and more than the queue holds.
static void testQueueDepth() {
	if (iopBuffer == 0) {
		return;
	}

	// More than the queue holds blocks inside SifSetDma rather than being
	// refused, so the sweep stops at what it takes.
	static const int counts[] = {1, 2, 4, 8};

	printf("Several transfers in one call:\n");
	for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
		static SifDmaTransfer_t transfers[8];
		for (int j = 0; j < counts[i]; ++j) {
			transfers[j].src = source;
			transfers[j].dest = iopBuffer;
			transfers[j].size = 64;
			transfers[j].attr = 0;
		}

		const int identifier = SifSetDma(transfers, counts[i]);
		printf("  %2d transfers: identifier %d, finished after %d checks\n",
		       counts[i], identifier, waitFor(identifier));
	}
}

// The status call on identifiers the queue never issued.
static void testStatusOfNothing() {
	static const int identifiers[] = {0, 1, 2, 0x7F, 0x100, -1, 0x7FFFFFFF};

	printf("The status of an identifier that was never issued:\n");
	for (unsigned i = 0; i < sizeof(identifiers) / sizeof(identifiers[0]); ++i) {
		printf("  %11d: %d\n", identifiers[i], SifDmaStat(identifiers[i]));
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testStatusOfNothing();
	testHeap();
	testRoundTrip();
	testAlignment();
	testAttributes();
	testQueueDepth();

	if (iopBuffer != 0) {
		printf("Freeing the buffer: %d\n", SifFreeIopHeap(iopBuffer));
	}

	printf("-- TEST END\n");
	return 0;
}
