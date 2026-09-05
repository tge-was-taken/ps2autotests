#include <common-ee.h>
#include <ee_regs.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "../dma/dmaregs.h"
#include "../dma/dmasend.h"
#include "../dma/vif/vifregs.h"
#include "emit_giftag.h"
#include "gsregs.h"

// Local to host transfers, which turn the bus around and read pixels back out.
// A read that never arrives would hang the machine, so every wait here is
// bounded and every attempt puts the bus back the way it was, whether or not
// anything came out.

static const u8 regBitbltbuf = 0x50;
static const u8 regTrxpos = 0x51;
static const u8 regTrxreg = 0x52;
static const u8 regTrxdir = 0x53;

static u32 *upload = 0;
static u32 *download = 0;
static const u32 bufferBytes = 8192;

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

// The count of quadwords waiting in VIF1, which is where the data arrives.
static u32 fifoCount() {
	return (VIF::VIF1->stat.bits_ >> 24) & 0x1F;
}

static void waitForGif() {
	for (int i = 0; i < 200000 && (*R_EE_GIF_STAT & ((3 << 10) | (1 << 9))); ++i) {
		continue;
	}
}

// Put the unit back whatever happened, so a transfer that stalled cannot take
// the next test with it.
static void restoreBus() {
	VIF::VIF1->stat = (VIF::RegSTATBits)0;
	VIF::VIF1->fbrst = VIF::FBRST_RST;
	*GS::BUSDIR = 0;
	*R_EE_GIF_CTRL = 1;
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
	*GS::CSR = GS::CSR_SIGNAL;
}

static void fillUpload(u32 words, u32 tag) {
	for (u32 i = 0; i < words; ++i) {
		upload[i] = (tag << 24) | i;
	}
	SyncDCache(upload, (u8 *)upload + words * 4);
}

// The known pattern this test reads back, put into graphics memory the way
// `transfer` already showed works.
static void sendToLocal(u32 base, u32 width, u32 format, u32 rectWidth,
                        u32 rectHeight, u32 quadwords) {
	GIF::PackedPacket packet(8192);

	GIF::Tag setup;
	setup.SetLoops(4);
	setup.SetEop(false);
	setup.SetRegDescs(GIF::REG_AD);
	packet.WriteTag(setup);
	packet.AD(regBitbltbuf, bitbltbuf(0, 1, 0, base, width, format));
	packet.AD(regTrxpos, trxpos(0, 0, 0, 0, 0));
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

	DMA::SendSimple(DMA::D2, packet.Raw(), (6 + quadwords) * 16);
	waitForGif();
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
}

struct Outcome {
	u32 received;
	u32 gifStat;
	u32 busdir;
	u32 csr;
};

// The register writes that turn the bus around, sent on their own so the unit
// has nothing queued behind them when the direction changes.
static void requestLocalToHost(u32 base, u32 width, u32 format, u32 x, u32 y,
                               u32 rectWidth, u32 rectHeight) {
	GIF::PackedPacket packet(1024);

	GIF::Tag setup;
	setup.SetLoops(4);
	setup.SetEop();
	setup.SetRegDescs(GIF::REG_AD);
	packet.WriteTag(setup);
	packet.AD(regBitbltbuf, bitbltbuf(base, width, format, 0, 1, 0));
	packet.AD(regTrxpos, trxpos(x, y, 0, 0, 0));
	packet.AD(regTrxreg, trxreg(rectWidth, rectHeight));
	packet.AD(regTrxdir, 1);

	DMA::SendSimple(DMA::D2, packet.Raw(), 5 * 16);
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
	waitForGif();
}

// Carried to memory by VIF1's own channel rather than read a quadword at a
// time: polling the fifo by hand loses entries when the unit sends faster than
// the loop reads.
static int drainToMemory(u32 wanted, u32 *out) {
	*GS::BUSDIR = 1;
	VIF::VIF1->stat = VIF::STAT_FDR;

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D1->madr = out;
	DMA::D1->qwc = wanted;
	DMA::D1->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);

	int spins = 0;
	for (; spins < 1000000 && DMA::D1->chcr.Ongoing(); ++spins) {
		continue;
	}
	const u32 left = DMA::D1->qwc;
	DMA::D1->chcr = (DMA::ChannelRegCHCRBits)0;

	SyncDCache(out, (u8 *)out + wanted * 16);
	return (int)(wanted - left);
}

// Reading the fifo by hand, which loses entries but says how the unit paces
// the data out.
static void readByDma(u32 wanted, Outcome &outcome) {
	memset(download, 0, bufferBytes);
	outcome.received = (u32)drainToMemory(wanted, download);
	outcome.gifStat = *R_EE_GIF_STAT;
	outcome.busdir = (u32)*GS::BUSDIR;
	outcome.csr = (u32)*GS::CSR;
	restoreBus();
}

static void readFifo(u32 wanted, Outcome &outcome) {
	volatile u128 *const destination = (volatile u128 *)download;

	memset(download, 0, bufferBytes);
	outcome.received = 0;

	// The unit only sends the other way once the bus is turned around and VIF1
	// is put in reverse; without the second of those nothing ever arrives.
	*GS::BUSDIR = 1;
	VIF::VIF1->stat = VIF::STAT_FDR;

	for (u32 spins = 0; spins < 400000 && outcome.received < wanted; ++spins) {
		if (fifoCount() == 0) {
			continue;
		}
		destination[outcome.received] = *VIF::VIF1_FIFO;
		outcome.received++;
	}

	outcome.gifStat = *R_EE_GIF_STAT;
	outcome.busdir = (u32)*GS::BUSDIR;
	outcome.csr = (u32)*GS::CSR;
	restoreBus();
}

static void printOutcome(const char *what, const Outcome &outcome, u32 wanted) {
	printf("  %-26s got %2d of %2d, gif %08x busdir %08x csr %08x\n", what,
	       outcome.received, wanted, outcome.gifStat, outcome.busdir, outcome.csr);
	if (outcome.received > 0) {
		printf("    first quadword %08x %08x %08x %08x\n", download[0], download[1],
		       download[2], download[3]);
	}
	if (outcome.received > 1) {
		printf("    second quadword %08x %08x %08x %08x\n", download[4],
		       download[5], download[6], download[7]);
	}
}

// The whole path on one small rectangle, which is what everything else needs
// to work before it is worth trying.
static void testSmallRectangle() {
	static const u32 base = 0x1000;

	printf("A 64 by 2 rectangle written and read back:\n");

	fillUpload(bufferBytes / 4, 0xAB);
	*R_EE_GIF_CTRL = 1;
	sendToLocal(base, 1, 0, 64, 2, 32);

	Outcome outcome;
	requestLocalToHost(base, 1, 0, 0, 0, 64, 2);
	readByDma(32, outcome);
	printOutcome("psmct32, by channel", outcome, 32);

	if (outcome.received > 0) {
		u32 same = 0;
		for (u32 i = 0; i < outcome.received * 4; ++i) {
			if (download[i] == upload[i]) {
				same++;
			}
		}
		printf("    %d of %d words match what went in\n", same,
		       outcome.received * 4);
	}

	// The same rectangle again, read a quadword at a time, so the two forms sit
	// next to each other in the record.
	requestLocalToHost(base, 1, 0, 0, 0, 64, 2);
	readFifo(32, outcome);
	printOutcome("psmct32, by hand", outcome, 32);
}

// The same rectangle read a quadword at a time with the count checked between,
// which says how the unit paces the data out.
static void testFifoFilling() {
	static const u32 base = 0x1000;

	printf("The fifo count as the data comes out:\n");

	*R_EE_GIF_CTRL = 1;
	requestLocalToHost(base, 1, 0, 0, 0, 64, 2);

	*GS::BUSDIR = 1;
	VIF::VIF1->stat = VIF::STAT_FDR;
	printf("   ");
	for (int i = 0; i < 12; ++i) {
		printf(" %2d", fifoCount());
		if (fifoCount() > 0) {
			volatile u128 discard = *VIF::VIF1_FIFO;
			(void)discard;
		}
	}
	printf("\n");
	restoreBus();
}

// Rectangles of each shape, so a wrong walk shows up as a wrong count rather
// than as wrong pixels.
static void testShapes() {
	struct Shape {
		const char *name;
		u32 width;
		u32 height;
		u32 quadwords;
	};
	static const Shape shapes[] = {
		{"1 by 1  ", 1, 1, 1},
		{"4 by 1  ", 4, 1, 1},
		{"8 by 1  ", 8, 1, 2},
		{"1 by 8  ", 1, 8, 2},
		{"16 by 4 ", 16, 4, 16},
		{"64 by 1 ", 64, 1, 16},
		{"3 by 3  ", 3, 3, 3},
	};

	printf("Each shape read back:\n");
	fillUpload(bufferBytes / 4, 0xCD);

	for (unsigned i = 0; i < sizeof(shapes) / sizeof(shapes[0]); ++i) {
		Outcome outcome;

		*R_EE_GIF_CTRL = 1;
		sendToLocal(0x1000, 1, 0, shapes[i].width, shapes[i].height,
		            shapes[i].quadwords);
		requestLocalToHost(0x1000, 1, 0, 0, 0, shapes[i].width, shapes[i].height);
		readByDma(shapes[i].quadwords, outcome);
		printOutcome(shapes[i].name, outcome, shapes[i].quadwords);
	}
}

// Each pixel format, whose bytes per pixel decide how many quadwords one
// rectangle is.
static void testFormats() {
	struct Format {
		const char *name;
		u32 value;
		u32 width;
		u32 height;
		u32 quadwords;
	};
	static const Format formats[] = {
		{"psmct32 ", 0x00, 64, 2, 32},
		{"psmct24 ", 0x01, 64, 2, 24},
		{"psmct16 ", 0x02, 64, 2, 16},
		{"psmct16s", 0x0A, 64, 2, 16},
		{"psmt8   ", 0x13, 64, 2, 8},
		{"psmt4   ", 0x14, 64, 2, 4},
	};

	printf("Each format read back:\n");
	fillUpload(bufferBytes / 4, 0xEF);

	for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		Outcome outcome;

		*R_EE_GIF_CTRL = 1;
		sendToLocal(0x2000, 1, formats[i].value, formats[i].width,
		            formats[i].height, formats[i].quadwords);
		requestLocalToHost(0x2000, 1, formats[i].value, 0, 0, formats[i].width,
		                   formats[i].height);
		readByDma(formats[i].quadwords, outcome);
		printOutcome(formats[i].name, outcome, formats[i].quadwords);
	}
}

// Reading with the bus never turned around, which is the case a program hits
// when it forgets the direction register.
static void testWithoutBusDirection() {
	Outcome outcome;

	printf("A read with the bus left pointing the other way:\n");

	*R_EE_GIF_CTRL = 1;
	sendToLocal(0x1000, 1, 0, 8, 1, 2);
	requestLocalToHost(0x1000, 1, 0, 0, 0, 8, 1);

	memset(download, 0, bufferBytes);
	outcome.received = 0;
	for (u32 spins = 0; spins < 100000 && outcome.received < 2; ++spins) {
		if (fifoCount() == 0) {
			continue;
		}
		((volatile u128 *)download)[outcome.received] = *VIF::VIF1_FIFO;
		outcome.received++;
	}
	outcome.gifStat = *R_EE_GIF_STAT;
	outcome.busdir = (u32)*GS::BUSDIR;
	outcome.csr = (u32)*GS::CSR;
	restoreBus();

	printOutcome("busdir left at zero", outcome, 2);
}

// Drawing into the same memory the transfer above proved it can read, so a
// buffer that comes back unchanged says the drawing did not land rather than
// that the transfer did not.  FRAME counts pages of 2048 bytes where BITBLTBUF
// counts blocks of 256, which is the conversion between the two numbers.
static void testDrawThenRead() {
	static const u32 base = 0x1000;
	static const u32 page = base / 8;

	printf("A sprite drawn into a buffer that was uploaded first:\n");

	fillUpload(bufferBytes / 4, 0x5C);
	*R_EE_GIF_CTRL = 1;
	sendToLocal(base, 1, 0, 64, 2, 32);

	Outcome outcome;
	requestLocalToHost(base, 1, 0, 0, 0, 64, 2);
	readByDma(32, outcome);
	printOutcome("before drawing", outcome, 32);

	// The offset the hardware is normally driven with puts the origin in the
	// middle of the coordinate space rather than at zero, so both are tried.
	static const u32 middle = 2048;

	*GS::SIGLBLID = 0;

	GIF::PackedPacket packet(1024);
	GIF::Tag tag;
	tag.SetLoops(16);
	tag.SetEop();
	tag.SetRegDescs(GIF::REG_AD);
	packet.WriteTag(tag);
	packet.AD(0x4C, (u64)page | (1ULL << 16));
	packet.AD(0x4E, (u64)(page + 1) | (1ULL << 32));
	packet.AD(0x18, 0);
	packet.AD(0x40, (63ULL << 16) | (1ULL << 48));
	packet.AD(0x47, (1ULL << 16) | (1ULL << 17));
	packet.AD(0x1A, 1);
	packet.AD(0x00, 6);
	packet.AD(0x01, 0x00804020);
	packet.AD(0x05, 0);
	packet.AD(0x05, (u64)(8 << 4) | ((u64)(2 << 4) << 16));
	// The same sprite again against an offset origin.
	packet.AD(0x18, (u64)(middle << 4) | ((u64)(middle << 4) << 32));
	packet.AD(0x00, 6);
	packet.AD(0x01, 0x00204080);
	packet.AD(0x05, (u64)(middle << 4) | ((u64)(middle << 4) << 16));
	packet.AD(0x05, (u64)((middle + 8) << 4) | ((u64)((middle + 2) << 4) << 16));
	packet.AD(0x62, GS::LABEL(0x0000ABCD, 0xFFFFFFFF));

	DMA::SendSimple(DMA::D2, packet.Raw(), 17 * 16);
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
	waitForGif();

	printf("  the label the draw packet ended with reads %016llx\n",
	       *GS::SIGLBLID);

	// A draw has more behind it than a transfer, so the unit is asked to say
	// when it has finished rather than assumed to have.
	*GS::CSR = 2;
	{
		GIF::PackedPacket finish(256);
		GIF::Tag tag2;
		tag2.SetLoops(1);
		tag2.SetEop();
		tag2.SetRegDescs(GIF::REG_AD);
		finish.WriteTag(tag2);
		finish.AD(0x61, 1);

		DMA::SendSimple(DMA::D2, finish.Raw(), 2 * 16);
		DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
	}
	int settled = 0;
	for (; settled < 1000000 && ((*GS::CSR >> 1) & 1) == 0; ++settled) {
		continue;
	}
	printf("  the unit reported finished after %d checks\n", settled);
	*GS::CSR = 2;

	requestLocalToHost(base, 1, 0, 0, 0, 64, 2);
	readByDma(32, outcome);
	printOutcome("after drawing", outcome, 32);
	printf("    words 0 to 7   %08x %08x %08x %08x %08x %08x %08x %08x\n",
	       download[0], download[1], download[2], download[3], download[4],
	       download[5], download[6], download[7]);
	printf("    words 64 to 71 %08x %08x %08x %08x %08x %08x %08x %08x\n",
	       download[64], download[65], download[66], download[67], download[68],
	       download[69], download[70], download[71]);

	u32 changed = 0;
	for (u32 i = 0; i < outcome.received * 4; ++i) {
		if (download[i] != upload[i]) {
			changed++;
		}
	}
	printf("    %d of %d words changed\n", changed, outcome.received * 4);

	// The two registers count in different units, so the drawing may have
	// landed at the address the other reading of FRAME gives.  Both are read
	// rather than argued about.
	static const u32 elsewhere[] = {page, base, base * 8, page * 8};
	for (unsigned i = 0; i < sizeof(elsewhere) / sizeof(elsewhere[0]); ++i) {
		requestLocalToHost(elsewhere[i], 1, 0, 0, 0, 64, 2);
		readByDma(32, outcome);
		printf("    read at %06x: %d quadwords, %08x %08x %08x %08x\n",
		       elsewhere[i], outcome.received, download[0], download[1],
		       download[2], download[3]);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	upload = (u32 *)memalign(64, bufferBytes);
	download = (u32 *)memalign(64, bufferBytes);

	testSmallRectangle();
	testDrawThenRead();
	testWithoutBusDirection();
	testFifoFilling();
	testShapes();
	testFormats();

	restoreBus();

	printf("-- TEST END\n");
	return 0;
}
