#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "drawprobe.h"

// The scissor rectangle and the offset that coordinates are measured from.
// Both clip the same sprite, so what is recorded is which pixels survive.

static u32 *frame = 0;

static void fillAndDraw(u64 scissor, u64 offset) {
	GS::resetDrawing();
	GS::clear(0x11111111);

	GS::Writes writes;
	writes.add(GS::REG_SCISSOR_1, scissor);
	writes.add(GS::REG_XYOFFSET_1, offset);
	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
	writes.add(GS::REG_RGBAQ, GS::rgbaq(0xFF, 0, 0, 0x80));
	writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
	writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
	writes.send();

	GS::readFrame(frame);
}

static u64 scissorOf(u32 x0, u32 x1, u32 y0, u32 y1) {
	return GS::scissor(x0, x1, y0, y1);
}

static void testRectangles() {
	struct Case {
		const char *name;
		u32 x0, x1, y0, y1;
	};
	static const Case cases[] = {
		{"the whole buffer", 0, 15, 0, 7},
		{"a quarter       ", 0, 7, 0, 3},
		{"one column      ", 4, 4, 0, 7},
		{"one pixel       ", 4, 4, 2, 2},
		{"nothing at all  ", 8, 4, 0, 7},
		{"one past the end", 0, 31, 0, 15},
		{"the right half  ", 8, 15, 0, 7},
		{"inverted rows   ", 0, 15, 6, 2},
	};

	printf("Each scissor rectangle:\n");
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		fillAndDraw(scissorOf(cases[i].x0, cases[i].x1, cases[i].y0, cases[i].y1),
		            0);
		GS::printDistinct(cases[i].name, frame);
		GS::printRow(cases[i].name, frame, 2);
	}
}

// The offset, which is a coordinate in sixteenths and so shifts by a fraction
// of a pixel as well as by whole ones.
static void testOffsets() {
	static const u32 offsets[] = {0, 1, 8, 16, 32, 0x8000};

	printf("Each offset:\n");
	for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
		fillAndDraw(scissorOf(0, 15, 0, 7),
		            (u64)offsets[i] | ((u64)offsets[i] << 32));

		char name[32];
		sprintf(name, "offset %5d/16", offsets[i]);
		GS::printDistinct(name, frame);
		GS::printRow(name, frame, 2);
	}
}

// A sprite whose corners are outside the buffer, which the offset has moved
// there rather than the scissor.
static void testOutside() {
	static const int positions[][4] = {
		{-4, -4, 4, 4}, {12, 4, 20, 12}, {-8, -8, -1, -1}, {20, 12, 30, 20},
	};

	printf("A sprite drawn partly outside:\n");
	for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		// The offset moves the origin, so a negative coordinate is written as a
		// positive one against a matching offset.
		writes.add(GS::REG_XYOFFSET_1, (16ULL * 16) | ((16ULL * 16) << 32));
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0, 0xFF, 0, 0x80));
		writes.add(GS::REG_XYZ2,
		           GS::xyz((u32)(positions[i][0] + 16), (u32)(positions[i][1] + 16), 0));
		writes.add(GS::REG_XYZ2,
		           GS::xyz((u32)(positions[i][2] + 16), (u32)(positions[i][3] + 16), 0));
		writes.send();

		GS::readFrame(frame);
		char name[32];
		sprintf(name, "%d,%d to %d,%d", positions[i][0], positions[i][1],
		        positions[i][2], positions[i][3]);
		GS::printDistinct(name, frame);
	}
}

// The row mask, which draws only the even or only the odd rows.
static void testScanMask() {
	printf("The row mask:\n");
	for (u32 mask = 0; mask < 4; ++mask) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		writes.add(GS::REG_SCANMSK, mask);
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0, 0, 0xFF, 0x80));
		writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
		writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
		writes.send();

		GS::readFrame(frame);
		char name[24];
		sprintf(name, "mask %d", mask);
		GS::printDistinct(name, frame);
		printf("    column 0 down: ");
		for (int y = 0; y < GS::probeHeight; ++y) {
			printf(" %08x", GS::pixelAt(frame, 0, y));
		}
		printf("\n");
	}
	GS::write(GS::REG_SCANMSK, 0);
}

// The whole coordinate range, since a coordinate is twelve bits of pixel and
// four of fraction.
static void testCoordinateRange() {
	static const u32 values[] = {0, 1, 0x7FF, 0x800, 0xFFF, 0x1000, 0xFFFF};

	printf("A sprite from the origin to each coordinate:\n");
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0xFF, 0xFF, 0xFF, 0x80));
		writes.add(GS::REG_XYZ2, 0);
		writes.add(GS::REG_XYZ2, (u64)values[i] | ((u64)values[i] << 16));
		writes.send();

		GS::readFrame(frame);
		char name[24];
		sprintf(name, "to %04x", values[i]);
		GS::printDistinct(name, frame);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, GS::probeWidth * GS::probeHeight * 4);

	testRectangles();
	testOffsets();
	testOutside();
	testScanMask();
	testCoordinateRange();

	printf("-- TEST END\n");
	return 0;
}
