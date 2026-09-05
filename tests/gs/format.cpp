#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "drawprobe.h"

// Where a pixel lands in memory for each format.  One pixel is written at a
// known position and the whole area is read back as raw words, so the answer is
// the offset the word turned up at rather than a picture.

static u32 *frame = 0;
static const int probeQuadwords = 64;

static void setFrame(u32 format) {
	GS::write(GS::REG_FRAME_1, (u64)GS::frameBase | ((u64)GS::frameWidth << 16) |
	                               ((u64)format << 24));
}

// Reads the probe rectangle back as raw 32-bit words whatever was drawn into
// it, so two formats can be compared against the same bytes.  The count has to
// match the rectangle the transfer names or the channel stops with quadwords
// still in the fifo.
static int readRaw() {
	return GS::readFrameAs(0, GS::probeWidth * GS::probeHeight / 4, frame);
}

static void clearPage(u32 format) {
	GS::resetDrawing();
	setFrame(format);

	GS::Writes writes;
	writes.add(GS::REG_SCISSOR_1, GS::scissor(0, 63, 0, 31));
	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
	writes.add(GS::REG_RGBAQ, 0);
	writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
	writes.add(GS::REG_XYZ2, GS::xyz(64, 32, 0));
	writes.send();
}

static void drawPixel(int x, int y, u32 colour) {
	GS::Writes writes;

	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
	writes.add(GS::REG_RGBAQ, colour);
	writes.add(GS::REG_XYZ2, GS::xyz(x, y, 0));
	writes.add(GS::REG_XYZ2, GS::xyz(x + 1, y + 1, 0));
	writes.send();
}

// Which word of the page a pixel at each position lands in.
static int findNonZero() {
	for (int i = 0; i < probeQuadwords * 4; ++i) {
		if (frame[i] != 0) {
			return i;
		}
	}
	return -1;
}

static void testPositions(const char *name, u32 format) {
	static const int positions[][2] = {
		{0, 0}, {1, 0}, {7, 0}, {8, 0}, {15, 0}, {16, 0},
		{0, 1}, {0, 2}, {0, 4}, {0, 7}, {0, 8},
		{1, 1}, {8, 8}, {31, 15},
	};

	printf("%s:\n", name);
	for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
		clearPage(format);
		drawPixel(positions[i][0], positions[i][1],
		          GS::rgbaq(0xFF, 0xFF, 0xFF, 0xFF));
		readRaw();

		const int word = findNonZero();
		printf("  %2d,%2d lands at word %4d", positions[i][0], positions[i][1],
		       word);
		if (word >= 0) {
			printf(" holding %08x", frame[word]);
		}
		printf("\n");
	}
}

// The colour a full white pixel becomes in each format, which is the rounding
// the narrow ones do.
static void testColourRounding(const char *name, u32 format) {
	static const u32 colours[] = {0x000000FF, 0x00FFFFFF, 0x0000FF00, 0x00FF0000,
	                              0x000000F8, 0x00000008};

	printf("%s, one pixel of each colour:\n", name);
	for (unsigned i = 0; i < sizeof(colours) / sizeof(colours[0]); ++i) {
		clearPage(format);
		drawPixel(0, 0, colours[i] | 0x80000000);
		readRaw();
		printf("  %08x becomes %08x\n", colours[i], frame[0]);
	}
}

// The buffer width field, which counts in units of sixty four pixels and
// decides how far one row is from the next.
static void testBufferWidth() {
	static const u32 widths[] = {1, 2, 4, 8};

	printf("The buffer width:\n");
	for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
		GS::resetDrawing();
		GS::write(GS::REG_FRAME_1,
		          (u64)GS::frameBase | ((u64)widths[i] << 16));

		GS::Writes writes;
		writes.add(GS::REG_SCISSOR_1, GS::scissor(0, 63, 0, 31));
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, 0);
		writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
		writes.add(GS::REG_XYZ2, GS::xyz(64, 32, 0));
		writes.send();

		drawPixel(0, 1, GS::rgbaq(0xFF, 0xFF, 0xFF, 0xFF));
		readRaw();
		printf("  width %d: the pixel at 0,1 lands at word %d\n", widths[i],
		       findNonZero());
	}
}

// The base address, which counts in pages of two thousand and forty eight
// bytes.
static void testBaseAddress() {
	static const u32 bases[] = {0x0100, 0x0101, 0x0102, 0x0108};

	printf("The base address:\n");
	for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
		GS::resetDrawing();
		GS::write(GS::REG_FRAME_1, (u64)bases[i] | ((u64)GS::frameWidth << 16));

		GS::Writes writes;
		writes.add(GS::REG_SCISSOR_1, GS::scissor(0, 15, 0, 7));
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0x10 + i, 0x20, 0x30, 0x80));
		writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
		writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
		writes.send();

		GS::readFrame(frame);
		printf("  base %04x: the buffer at %04x reads %08x\n", bases[i],
		       GS::frameBase, frame[0]);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, probeQuadwords * 16);

	testPositions("32 bits a pixel", 0x00);
	testPositions("16 bits a pixel", 0x02);
	testPositions("16 bits a pixel, the other one", 0x0A);
	testPositions("24 bits a pixel", 0x01);
	testColourRounding("32 bits a pixel", 0x00);
	testColourRounding("16 bits a pixel", 0x02);
	testColourRounding("24 bits a pixel", 0x01);
	testBufferWidth();
	testBaseAddress();

	printf("-- TEST END\n");
	return 0;
}
