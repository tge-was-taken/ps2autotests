#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "drawprobe.h"

// The two drawing contexts, which are two copies of every register a primitive
// reads.  Which one a primitive uses comes from a bit in the primitive itself,
// so the two are set differently and the bit is flipped between draws.

static u32 *frame = 0;

static void setBothContexts() {
	GS::Writes writes;

	writes.add(GS::REG_FRAME_1, (u64)GS::frameBase | ((u64)GS::frameWidth << 16));
	writes.add(GS::REG_FRAME_2, (u64)GS::frameBase | ((u64)GS::frameWidth << 16));
	writes.add(GS::REG_ZBUF_1, (u64)GS::zBase | (1ULL << 32));
	writes.add(GS::REG_ZBUF_2, (u64)GS::zBase | (1ULL << 32));
	writes.add(GS::REG_XYOFFSET_1, 0);
	writes.add(GS::REG_XYOFFSET_2, 0);
	writes.add(GS::REG_TEST_1, GS::testAlways);
	writes.add(GS::REG_TEST_2, GS::testAlways);
	writes.add(GS::REG_ALPHA_1, 0);
	writes.add(GS::REG_ALPHA_2, 0);
	writes.add(GS::REG_SCISSOR_1, GS::scissor(0, 15, 0, 7));
	writes.add(GS::REG_SCISSOR_2, GS::scissor(0, 15, 0, 7));
	writes.send();
}

static void drawWithContext(u32 context, u32 colour, int x0, int y0, int x1,
                            int y1) {
	GS::Writes writes;

	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, context << 6));
	writes.add(GS::REG_RGBAQ, colour);
	writes.add(GS::REG_XYZ2, GS::xyz(x0, y0, 0));
	writes.add(GS::REG_XYZ2, GS::xyz(x1, y1, 0));
	writes.send();
}

// Two scissor rectangles, one per context, so the bit decides which clip
// applies.
static void testScissorPerContext() {
	printf("A different scissor in each context:\n");

	GS::resetDrawing();
	setBothContexts();
	GS::clear(0x11111111);

	GS::Writes writes;
	writes.add(GS::REG_SCISSOR_1, GS::scissor(0, 7, 0, 7));
	writes.add(GS::REG_SCISSOR_2, GS::scissor(8, 15, 0, 7));
	writes.send();

	drawWithContext(0, GS::rgbaq(0xFF, 0, 0, 0x80), 0, 0, 16, 4);
	GS::readFrame(frame);
	GS::printRow("context 0 only", frame, 1);

	drawWithContext(1, GS::rgbaq(0, 0xFF, 0, 0x80), 0, 4, 16, 8);
	GS::readFrame(frame);
	GS::printRow("context 1 as well", frame, 5);
	GS::printDistinct("both", frame);
}

// The offset, which each context holds its own copy of.
static void testOffsetPerContext() {
	printf("A different offset in each context:\n");

	GS::resetDrawing();
	setBothContexts();
	GS::clear(0x11111111);

	GS::Writes writes;
	writes.add(GS::REG_XYOFFSET_1, 0);
	writes.add(GS::REG_XYOFFSET_2, (4ULL * 16) | ((2ULL * 16) << 32));
	writes.send();

	drawWithContext(0, GS::rgbaq(0xFF, 0, 0, 0x80), 0, 0, 4, 2);
	drawWithContext(1, GS::rgbaq(0, 0, 0xFF, 0x80), 0, 0, 4, 2);
	GS::readFrame(frame);

	GS::printRow("row 0", frame, 0);
	GS::printRow("row 2", frame, 2);
	GS::printDistinct("both", frame);
}

// The test register, so one context rejects what the other accepts.
static void testTestPerContext() {
	printf("A different pixel test in each context:\n");

	GS::resetDrawing();
	setBothContexts();
	GS::clear(0x11111111);

	GS::Writes writes;
	// Context one rejects everything through its alpha test, context zero
	// accepts everything.
	writes.add(GS::REG_TEST_1, GS::testAlways);
	writes.add(GS::REG_TEST_2, GS::testAlways | 1ULL);
	writes.send();

	drawWithContext(0, GS::rgbaq(0xFF, 0xFF, 0, 0x80), 0, 0, 8, 8);
	drawWithContext(1, GS::rgbaq(0, 0xFF, 0xFF, 0x80), 8, 0, 16, 8);
	GS::readFrame(frame);

	GS::printRow("both drawn", frame, 3);
	GS::printDistinct("both", frame);
}

// The frame register, so the two contexts write to different places.
static void testFramePerContext() {
	printf("A different frame buffer in each context:\n");

	GS::resetDrawing();
	setBothContexts();
	GS::clear(0x11111111);

	GS::Writes writes;
	writes.add(GS::REG_FRAME_2, (u64)(GS::frameBase + 0x20) |
	                                ((u64)GS::frameWidth << 16));
	writes.send();

	drawWithContext(1, GS::rgbaq(0xFF, 0, 0xFF, 0x80), 0, 0, 16, 8);
	GS::readFrame(frame);
	GS::printDistinct("after drawing to the other buffer", frame);

	drawWithContext(0, GS::rgbaq(0, 0x80, 0, 0x80), 0, 0, 16, 8);
	GS::readFrame(frame);
	GS::printDistinct("after drawing to this one", frame);
}

// The write mask in the frame register, which each context also holds.
static void testWriteMask() {
	static const u32 masks[] = {0x00000000, 0xFF000000, 0x0000FFFF, 0xFFFFFFFF,
	                            0x00FF00FF};

	printf("The frame write mask:\n");
	for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		writes.add(GS::REG_FRAME_1, (u64)GS::frameBase |
		                                ((u64)GS::frameWidth << 16) |
		                                ((u64)masks[i] << 32));
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0xAA, 0xBB, 0xCC, 0xDD));
		writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
		writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
		writes.send();

		GS::readFrame(frame);
		char name[24];
		sprintf(name, "mask %08x", masks[i]);
		GS::printDistinct(name, frame);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, GS::probeWidth * GS::probeHeight * 4);

	testScissorPerContext();
	testOffsetPerContext();
	testTestPerContext();
	testFramePerContext();
	testWriteMask();

	printf("-- TEST END\n");
	return 0;
}
