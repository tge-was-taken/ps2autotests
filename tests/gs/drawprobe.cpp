#include <common-ee.h>
#include <ee_regs.h>
#include <kernel.h>
#include <string.h>
#include "../dma/dmaregs.h"
#include "../dma/dmasend.h"
#include "../dma/vif/vifregs.h"
#include "drawprobe.h"
#include "gsregs.h"

namespace GS {

u64 scissor(u32 x0, u32 x1, u32 y0, u32 y1) {
	return (u64)x0 | ((u64)x1 << 16) | ((u64)y0 << 32) | ((u64)y1 << 48);
}

u64 rgbaq(u32 r, u32 g, u32 b, u32 a) {
	return (u64)r | ((u64)g << 8) | ((u64)b << 16) | ((u64)a << 24);
}

u64 xyz(u32 x, u32 y, u32 z) {
	return (u64)(x << 4) | ((u64)(y << 4) << 16) | ((u64)z << 32);
}

u64 prim(u32 type, u32 flags) {
	return (u64)type | ((u64)flags << 3);
}

Writes::Writes() : count_(0) {
}

void Writes::add(u8 address, u64 value) {
	if (count_ >= limit) {
		return;
	}
	addresses_[count_] = address;
	values_[count_] = value;
	count_++;
}

void Writes::send() {
	if (count_ == 0) {
		return;
	}

	GIF::PackedPacket packet(16 + limit * 16);
	GIF::Tag tag;
	tag.SetLoops(count_);
	tag.SetEop();
	tag.SetRegDescs(GIF::REG_AD);
	packet.WriteTag(tag);
	for (u32 i = 0; i < count_; ++i) {
		packet.AD(addresses_[i], values_[i]);
	}

	DMA::SendSimple(DMA::D2, packet.Raw(), packet.Size());
	for (int i = 0; i < 200000 && (*R_EE_GIF_STAT & ((3 << 10) | (1 << 9))); ++i) {
		continue;
	}
	count_ = 0;
}

void write(u8 address, u64 value) {
	Writes writes;
	writes.add(address, value);
	writes.send();
}

// Every packet that draws carries the whole state it needs, since a state left
// over from an earlier packet does not survive the transfer that reads the
// buffer back.
static void addState(Writes &writes) {
	writes.add(REG_FRAME_1, (u64)frameBase | ((u64)frameWidth << 16));
	writes.add(REG_ZBUF_1, (u64)zBase | ((u64)0 << 24) | (1ULL << 32));
	writes.add(REG_XYOFFSET_1, 0);
	writes.add(REG_SCISSOR_1, scissor(0, probeWidth - 1, 0, probeHeight - 1));
	writes.add(REG_TEST_1, testAlways);
	writes.add(REG_ALPHA_1, 0);
	writes.add(REG_COLCLAMP, 1);
	writes.add(REG_DTHE, 0);
	writes.add(REG_PABE, 0);
	writes.add(REG_FBA_1, 0);
	writes.add(REG_PRMODECONT, 1);
	writes.add(REG_TEXA, 0x0000008000000080ULL);
	writes.add(REG_SCANMSK, 0);
	writes.add(REG_FOGCOL, 0);
}

void resetDrawing() {
	Writes writes;
	addState(writes);
	writes.send();
}

void drawSprite(int x0, int y0, int x1, int y1, u32 colour) {
	Writes writes;

	addState(writes);
	writes.add(REG_PRIM, prim(PRIM_SPRITE, 0));
	writes.add(REG_RGBAQ, colour);
	writes.add(REG_XYZ2, xyz(x0, y0, 0));
	writes.add(REG_XYZ2, xyz(x1, y1, 0));
	writes.send();
}

void clear(u32 colour) {
	Writes writes;

	addState(writes);
	writes.add(REG_PRIM, prim(PRIM_SPRITE, 0));
	writes.add(REG_RGBAQ, colour);
	writes.add(REG_XYZ2, xyz(0, 0, 0));
	writes.add(REG_XYZ2, xyz(probeWidth, probeHeight, 0));
	writes.send();
}

int readFrameAs(u32 format, int quadwords, u32 *out) {
	memset(out, 0, quadwords * 16);

	{
		GIF::PackedPacket packet(1024);
		GIF::Tag tag;
		tag.SetLoops(4);
		tag.SetEop();
		tag.SetRegDescs(GIF::REG_AD);
		packet.WriteTag(tag);
		packet.AD(REG_BITBLTBUF, (u64)frameBase | ((u64)frameWidth << 16) |
		                             ((u64)format << 24));
		packet.AD(REG_TRXPOS, 0);
		packet.AD(REG_TRXREG, (u64)probeWidth | ((u64)probeHeight << 32));
		packet.AD(REG_TRXDIR, 1);

		DMA::SendSimple(DMA::D2, packet.Raw(), 5 * 16);
		DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
	}

	int received = 0;

	// The data comes back through VIF1 rather than through the graphics fifo,
	// and its channel carries it to memory: polling the fifo by hand loses
	// entries when the unit sends faster than the loop reads.
	*BUSDIR = 1;
	VIF::VIF1->stat = VIF::STAT_FDR;

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D1->madr = out;
	DMA::D1->qwc = quadwords;
	DMA::D1->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);
	for (int spins = 0; spins < 1000000 && DMA::D1->chcr.Ongoing(); ++spins) {
		continue;
	}
	received = quadwords - (int)DMA::D1->qwc;
	DMA::D1->chcr = (DMA::ChannelRegCHCRBits)0;
	SyncDCache(out, (u8 *)out + quadwords * 16);

	VIF::VIF1->stat = (VIF::RegSTATBits)0;
	VIF::VIF1->fbrst = VIF::FBRST_RST;
	*BUSDIR = 0;
	*R_EE_GIF_CTRL = 1;
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;

	return received;
}

int readFrame(u32 *out) {
	return readFrameAs(0, probeWidth * probeHeight / 4, out);
}

void reportFrameAddress() {
	static u32 found[probeWidth * probeHeight];
	static const u32 candidates[] = {frameBase, frameBase * 8, frameBase * 64,
	                                 frameBase / 8, 0};

	resetDrawing();
	clear(0x11111111);
	drawSprite(0, 0, probeWidth, probeHeight, rgbaq(0x33, 0x44, 0x55, 0x80));

	printf("Where a full sprite lands, with FRAME set to page %04x:\n", frameBase);
	for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
		GIF::PackedPacket packet(1024);
		GIF::Tag tag;
		tag.SetLoops(4);
		tag.SetEop();
		tag.SetRegDescs(GIF::REG_AD);
		packet.WriteTag(tag);
		packet.AD(REG_BITBLTBUF, (u64)candidates[i] | ((u64)frameWidth << 16));
		packet.AD(REG_TRXPOS, 0);
		packet.AD(REG_TRXREG, (u64)probeWidth | ((u64)probeHeight << 32));
		packet.AD(REG_TRXDIR, 1);
		DMA::SendSimple(DMA::D2, packet.Raw(), 5 * 16);
		DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
		for (int spins = 0; spins < 200000 &&
		                    (*R_EE_GIF_STAT & ((3 << 10) | (1 << 9))); ++spins) {
			continue;
		}

		memset(found, 0, sizeof(found));
		*BUSDIR = 1;
		VIF::VIF1->stat = VIF::STAT_FDR;
		*DMA::D_CTRL = DMA::D_CTRL_DMAE;
		DMA::D1->madr = found;
		DMA::D1->qwc = probeWidth * probeHeight / 4;
		DMA::D1->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR);
		for (int spins = 0; spins < 1000000 && DMA::D1->chcr.Ongoing(); ++spins) {
			continue;
		}
		DMA::D1->chcr = (DMA::ChannelRegCHCRBits)0;
		SyncDCache(found, found + probeWidth * probeHeight);
		VIF::VIF1->stat = (VIF::RegSTATBits)0;
		VIF::VIF1->fbrst = VIF::FBRST_RST;
		*BUSDIR = 0;
		*R_EE_GIF_CTRL = 1;

		printf("  block %06x: %08x %08x %08x %08x\n", candidates[i], found[0],
		       found[1], found[2], found[3]);
	}
}

u32 pixelAt(const u32 *frame, int x, int y) {
	return frame[y * probeWidth + x];
}

void printRow(const char *what, const u32 *frame, int y) {
	printf("  %-18s row %d:", what, y);
	for (int x = 0; x < probeWidth; ++x) {
		printf(" %08x", pixelAt(frame, x, y));
	}
	printf("\n");
}

void printCorners(const char *what, const u32 *frame) {
	printf("  %-18s %08x %08x %08x %08x\n", what, pixelAt(frame, 0, 0),
	       pixelAt(frame, probeWidth - 1, 0), pixelAt(frame, 0, probeHeight - 1),
	       pixelAt(frame, probeWidth - 1, probeHeight - 1));
}

// How many different values the buffer holds, which says whether a primitive
// covered anything without printing all one hundred and twenty eight pixels.
void printDistinct(const char *what, const u32 *frame) {
	u32 values[8];
	int counts[8];
	int found = 0;

	for (int i = 0; i < probeWidth * probeHeight; ++i) {
		int seen = -1;
		for (int j = 0; j < found; ++j) {
			if (values[j] == frame[i]) {
				seen = j;
				break;
			}
		}
		if (seen >= 0) {
			counts[seen]++;
		} else if (found < 8) {
			values[found] = frame[i];
			counts[found] = 1;
			found++;
		}
	}

	printf("  %-18s", what);
	for (int i = 0; i < found; ++i) {
		printf(" %08x x%d", values[i], counts[i]);
	}
	if (found == 8) {
		printf(" (and more)");
	}
	printf("\n");
}

}
