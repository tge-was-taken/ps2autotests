#include <common-ee.h>
#include <ee_regs.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "../dma/dmaregs.h"
#include "../dma/dmasend.h"
#include "emit_giftag.h"
#include "gsregs.h"

// Host to local transfers, which is the half of the path that can be measured
// without turning the bus around.  Nothing is read back: what is recorded is
// whether each transfer is accepted and what the status registers say
// afterwards, which is enough to pin how the unit walks a rectangle.

static const u8 regBitbltbuf = 0x50;
static const u8 regTrxpos = 0x51;
static const u8 regTrxreg = 0x52;
static const u8 regTrxdir = 0x53;

static u32 *upload = 0;

static u64 bitbltbuf(u32 sourceBase, u32 sourceWidth, u32 sourceFormat,
                     u32 destBase, u32 destWidth, u32 destFormat) {
	return ((u64)sourceBase) | ((u64)sourceWidth << 16) | ((u64)sourceFormat << 24) |
	       ((u64)destBase << 32) | ((u64)destWidth << 48) | ((u64)destFormat << 56);
}

static u64 trxpos(u32 sourceX, u32 sourceY, u32 destX, u32 destY, u32 direction) {
	return ((u64)sourceX) | ((u64)sourceY << 16) | ((u64)destX << 32) |
	       ((u64)destY << 48) | ((u64)direction << 59);
}

static u64 trxreg(u32 width, u32 height) {
	return ((u64)width) | ((u64)height << 32);
}

static void fillUpload(u32 words, u32 tag) {
	for (u32 i = 0; i < words; ++i) {
		upload[i] = (tag << 24) | i;
	}
	SyncDCache(upload, (u8 *)upload + words * 4);
}

struct Outcome {
	u32 gifStat;
	u32 chcr;
	u32 qwc;
	u32 csr;
};

static void waitForGif() {
	for (int i = 0; i < 100000 && (*R_EE_GIF_STAT & ((3 << 10) | (1 << 9))); ++i) {
		continue;
	}
}

static void collect(Outcome &outcome) {
	outcome.gifStat = *R_EE_GIF_STAT;
	outcome.chcr = DMA::D2->chcr.bits_;
	outcome.qwc = DMA::D2->qwc;
	outcome.csr = (u32)*GS::CSR;
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
}

// A transfer is a packet of four register writes followed by an image tag
// carrying the pixels: one quadword for the tag, four for the writes, one for
// the image tag, then the data.
static void sendToLocal(u32 base, u32 width, u32 format, u32 x, u32 y,
                        u32 rectWidth, u32 rectHeight, u32 quadwords,
                        Outcome &outcome) {
	GIF::PackedPacket packet(8192);

	GIF::Tag setup;
	setup.SetLoops(4);
	setup.SetEop(false);
	setup.SetRegDescs(GIF::REG_AD);
	packet.WriteTag(setup);
	packet.AD(regBitbltbuf, bitbltbuf(0, 1, 0, base, width, format));
	packet.AD(regTrxpos, trxpos(0, 0, x, y, 0));
	packet.AD(regTrxreg, trxreg(rectWidth, rectHeight));
	packet.AD(regTrxdir, 0);

	GIF::Tag image;
	image.SetLoops(quadwords);
	image.SetEop();
	image.SetFormat(GIF::FORMAT_IMAGE);
	packet.Packet::WriteTag(image);
	for (u32 i = 0; i < quadwords * 2; ++i) {
		packet.Emit(((u64 *)upload)[i]);
	}

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D2->madr = packet.Raw();
	DMA::D2->qwc = 6 + quadwords;
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR | DMA::CHCR_DIR);

	int spins = 1000000;
	while (--spins > 0 && DMA::D2->chcr.Ongoing()) {
		continue;
	}
	waitForGif();
	collect(outcome);
}

static void printOutcome(const char *what, const Outcome &outcome) {
	printf("  %-22s gif %08x chcr %08x qwc %d csr %08x\n", what, outcome.gifStat,
	       outcome.chcr, outcome.qwc, outcome.csr);
}

static void testRectangles() {
	static const u32 widths[] = {4, 8, 16, 32, 64};

	printf("Rectangles of 32 bit pixels, one row:\n");
	for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
		const u32 width = widths[i];
		fillUpload(width, 0xB0 + i);

		Outcome outcome;
		sendToLocal(0, 1, 0, 0, 0, width, 1, width / 4, outcome);

		char label[40];
		sprintf(label, "%2u wide", width);
		printOutcome(label, outcome);
	}
}

static void testHeights() {
	static const u32 heights[] = {1, 2, 4, 8};

	printf("Rectangles four pixels wide:\n");
	for (unsigned i = 0; i < sizeof(heights) / sizeof(heights[0]); ++i) {
		const u32 height = heights[i];
		fillUpload(4 * height, 0xC0 + i);

		Outcome outcome;
		sendToLocal(0, 1, 0, 0, 0, 4, height, height, outcome);

		char label[40];
		sprintf(label, "%u rows", height);
		printOutcome(label, outcome);
	}
}

// Each format packs a different number of pixels into a quadword, so the count
// the unit expects for one quadword differs.
static void testFormats() {
	struct Format {
		const char *name;
		u32 psm;
		u32 pixelsPerQuadword;
	};
	static const Format formats[] = {
		{"PSMCT32", 0x00, 4},   {"PSMCT24", 0x01, 5},  {"PSMCT16", 0x02, 8},
		{"PSMCT16S", 0x0A, 8},  {"PSMT8", 0x13, 16},   {"PSMT4", 0x14, 32},
		{"PSMT8H", 0x1B, 16},   {"PSMZ32", 0x30, 4},   {"PSMZ16", 0x32, 8},
	};

	printf("Pixel formats, one quadword each:\n");
	for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		fillUpload(4, 0xD0 + i);

		Outcome outcome;
		sendToLocal(0x100, 1, formats[i].psm, 0, 0, formats[i].pixelsPerQuadword, 1, 1,
		            outcome);
		printOutcome(formats[i].name, outcome);
	}
}

// A count that does not match the rectangle, which the unit has to either pad
// or refuse.
static void testMismatchedCounts() {
	static const u32 counts[] = {1, 2, 3, 5};

	printf("Eight pixels wide with a mismatched quadword count:\n");
	for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
		fillUpload(counts[i] * 4, 0xE0 + i);

		Outcome outcome;
		sendToLocal(0x200, 1, 0, 0, 0, 8, 1, counts[i], outcome);

		char label[40];
		sprintf(label, "%u quadwords for 2", counts[i]);
		printOutcome(label, outcome);
	}
}

// The destination coordinates, and one past the width the buffer register
// declares.
static void testOffsets() {
	static const u32 offsets[][2] = {{0, 0}, {4, 0}, {0, 1}, {8, 2}, {60, 0},
	                                 {64, 0}, {0, 63}};

	printf("Destination offsets, buffer one page wide:\n");
	for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
		fillUpload(4, 0xF0 + i);

		Outcome outcome;
		sendToLocal(0x300, 1, 0, offsets[i][0], offsets[i][1], 4, 1, 1, outcome);

		char label[40];
		sprintf(label, "x %2u y %2u", offsets[i][0], offsets[i][1]);
		printOutcome(label, outcome);
	}
}

// The buffer width field, which says how far one row is from the next.
static void testBufferWidth() {
	static const u32 widths[] = {1, 2, 4, 8, 0};

	printf("Buffer width, four rows of four pixels:\n");
	for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
		fillUpload(16, 0x80 + i);

		Outcome outcome;
		sendToLocal(0x400, widths[i], 0, 0, 0, 4, 4, 4, outcome);

		char label[40];
		sprintf(label, "width %u", widths[i]);
		printOutcome(label, outcome);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	upload = (u32 *)memalign(16, 8192);

	testRectangles();
	testHeights();
	testFormats();
	testMismatchedCounts();
	testOffsets();
	testBufferWidth();

	free(upload);

	printf("-- TEST END\n");
	return 0;
}
