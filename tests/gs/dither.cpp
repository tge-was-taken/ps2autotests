#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "drawprobe.h"

// The dither, which adds a small offset per pixel position when the target
// holds fewer bits than the colour.  A 32-bit target has nothing to dither, so
// the buffer here is 16 bits and read back through a 16-bit transfer.

static u32 *frame = 0;

static const u32 narrowFormat = 0x02;

static void setNarrowFrame() {
	GS::write(GS::REG_FRAME_1, (u64)GS::frameBase | ((u64)GS::frameWidth << 16) |
	                               ((u64)narrowFormat << 24));
}

// The buffer is read back as 16-bit pixels, so each word holds two of them.
static void printPairs(const char *what, int row) {
	printf("  %-22s", what);
	for (int i = 0; i < 8; ++i) {
		printf(" %08x", frame[row * 8 + i]);
	}
	printf("\n");
}

static void drawFlat(u32 colour, u64 matrix, u32 enabled) {
	GS::resetDrawing();
	setNarrowFrame();

	GS::Writes writes;
	writes.add(GS::REG_DIMX, matrix);
	writes.add(GS::REG_DTHE, enabled);
	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
	writes.add(GS::REG_RGBAQ, colour);
	writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
	writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
	writes.send();

	GS::readFrameAs(narrowFormat, GS::probeWidth * GS::probeHeight / 8, frame);
}

// The matrix the manual gives as an example, and the two that turn it off.
static void testMatrices() {
	struct Case {
		const char *name;
		u64 matrix;
	};
	static const Case cases[] = {
		{"all zero        ", 0x0000000000000000ULL},
		{"the example     ", 0x7B5D15977B5D1597ULL},
		{"all ones        ", 0x1111111111111111ULL},
		{"all sevens      ", 0x7777777777777777ULL},
		{"all fours       ", 0x4444444444444444ULL},
		{"every bit set   ", 0xFFFFFFFFFFFFFFFFULL},
	};

	printf("A flat mid grey with each matrix:\n");
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		drawFlat(GS::rgbaq(0x84, 0x84, 0x84, 0x80), cases[i].matrix, 1);
		printPairs(cases[i].name, 0);
		printPairs(cases[i].name, 1);
	}
}

// The switch, with the same matrix either side of it.
static void testSwitch() {
	printf("The dither switch:\n");
	for (u32 enabled = 0; enabled < 2; ++enabled) {
		drawFlat(GS::rgbaq(0x84, 0x84, 0x84, 0x80), 0x7B5D15977B5D1597ULL, enabled);
		char name[24];
		sprintf(name, "enabled %d", enabled);
		printPairs(name, 0);
	}
}

// Colours that sit exactly on a step of the narrow format, and ones between two
// steps, since only the second kind can dither.
static void testColours() {
	static const u32 levels[] = {0x00, 0x08, 0x80, 0x84, 0x88, 0xF8, 0xFF};

	printf("Each grey level with the example matrix:\n");
	for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
		drawFlat(GS::rgbaq(levels[i], levels[i], levels[i], 0x80),
		         0x7B5D15977B5D1597ULL, 1);
		char name[24];
		sprintf(name, "level %02x", levels[i]);
		printPairs(name, 0);
	}
}

// The same colour on a 32-bit target, which has nothing to round away.
static void testWideTarget() {
	printf("The same colour on a 32-bit target:\n");
	for (u32 enabled = 0; enabled < 2; ++enabled) {
		GS::resetDrawing();

		GS::Writes writes;
		writes.add(GS::REG_DIMX, 0x7B5D15977B5D1597ULL);
		writes.add(GS::REG_DTHE, enabled);
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0x84, 0x84, 0x84, 0x80));
		writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
		writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
		writes.send();

		GS::readFrame(frame);
		char name[24];
		sprintf(name, "enabled %d", enabled);
		GS::printDistinct(name, frame);
	}
}

// Whether the pattern repeats every four pixels, which is what a four by four
// matrix means.
static void testPeriod() {
	printf("The pattern down one column and across one row:\n");
	drawFlat(GS::rgbaq(0x84, 0x84, 0x84, 0x80), 0x7B5D15977B5D1597ULL, 1);
	for (int row = 0; row < 8; ++row) {
		char name[24];
		sprintf(name, "row %d", row);
		printPairs(name, row);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, GS::probeWidth * GS::probeHeight * 4);

	testSwitch();
	testMatrices();
	testColours();
	testPeriod();
	testWideTarget();

	printf("-- TEST END\n");
	return 0;
}
