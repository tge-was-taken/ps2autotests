#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "drawprobe.h"

// Each primitive type drawn into the same small buffer and read back, so the
// fill rule is measured from the pixels rather than from a picture.

static u32 *frame = 0;

static void showTypes() {
	struct Case {
		const char *name;
		GS::Primitive type;
		int vertices;
	};
	static const Case cases[] = {
		{"point         ", GS::PRIM_POINT, 1},
		{"line          ", GS::PRIM_LINE, 2},
		{"line strip    ", GS::PRIM_LINE_STRIP, 3},
		{"triangle      ", GS::PRIM_TRIANGLE, 3},
		{"triangle strip", GS::PRIM_TRIANGLE_STRIP, 4},
		{"triangle fan  ", GS::PRIM_TRIANGLE_FAN, 4},
		{"sprite        ", GS::PRIM_SPRITE, 2},
		{"the seventh   ", GS::PRIM_INVALID, 3},
	};
	static const u32 corners[8][2] = {{2, 2}, {12, 2}, {12, 6}, {2, 6},
	                                  {7, 4}, {3, 5},  {11, 3}, {6, 6}};

	printf("Each primitive type:\n");
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		writes.add(GS::REG_PRIM, GS::prim(cases[i].type, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0x80, 0x40, 0x20, 0x80));
		for (int v = 0; v < cases[i].vertices; ++v) {
			writes.add(GS::REG_XYZ2, GS::xyz(corners[v][0], corners[v][1], 0));
		}
		writes.send();

		const int received = GS::readFrame(frame);
		printf("  %s got %d quadwords\n", cases[i].name, received);
		GS::printDistinct(cases[i].name, frame);
	}
}

// A sprite one pixel wide and one tall at each corner, which is where a fill
// rule that is off by one shows first.
static void showEdges() {
	static const int positions[][4] = {
		{0, 0, 1, 1}, {0, 0, 0, 0}, {15, 7, 16, 8}, {15, 7, 15, 7},
		{0, 0, 16, 8}, {8, 4, 4, 2}, {4, 2, 8, 4},
	};

	printf("A sprite between each pair of corners:\n");
	for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
		GS::resetDrawing();
		GS::clear(0x11111111);
		GS::drawSprite(positions[i][0], positions[i][1], positions[i][2],
		               positions[i][3], GS::rgbaq(0xFF, 0, 0, 0x80));
		GS::readFrame(frame);

		char name[32];
		sprintf(name, "%d,%d to %d,%d", positions[i][0], positions[i][1],
		        positions[i][2], positions[i][3]);
		GS::printDistinct(name, frame);
	}
}

// The four bits of a coordinate below the whole pixel, which decide which side
// of the edge a pixel falls on.
static void showSubpixel() {
	printf("A sprite moved a sixteenth of a pixel at a time:\n");
	for (u32 step = 0; step < 8; ++step) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0, 0xFF, 0, 0x80));
		writes.add(GS::REG_XYZ2, (u64)((4 << 4) + step) | ((u64)(2 << 4) << 16));
		writes.add(GS::REG_XYZ2, (u64)((8 << 4) + step) | ((u64)(6 << 4) << 16));
		writes.send();

		GS::readFrame(frame);
		char name[24];
		sprintf(name, "offset %d/16", step);
		GS::printRow(name, frame, 3);
	}
}

// The flag bits in the primitive register, which turn on the shading, the
// texture, the fog and the blend at once.
static void showFlags() {
	static const u32 flags[] = {0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0xFF};

	printf("Each flag in the primitive register:\n");
	for (unsigned i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, flags[i]));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0x40, 0x60, 0x80, 0x40));
		writes.add(GS::REG_XYZ2, GS::xyz(2, 2, 0));
		writes.add(GS::REG_XYZ2, GS::xyz(10, 6, 0));
		writes.send();

		GS::readFrame(frame);
		char name[24];
		sprintf(name, "flags %02x", flags[i]);
		GS::printDistinct(name, frame);
	}
}

// The register that says whether the primitive register or the mode register
// carries the flags.
static void showModeControl() {
	printf("The mode control register:\n");
	for (u32 control = 0; control < 2; ++control) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		writes.add(GS::REG_PRMODECONT, control);
		writes.add(GS::REG_PRMODE, GS::prim(0, 0x08));
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0x40, 0x60, 0x80, 0x40));
		writes.add(GS::REG_XYZ2, GS::xyz(2, 2, 0));
		writes.add(GS::REG_XYZ2, GS::xyz(10, 6, 0));
		writes.send();

		GS::readFrame(frame);
		char name[24];
		sprintf(name, "control %d", control);
		GS::printDistinct(name, frame);
	}
}

// A vertex that does not kick, which is how a strip is started without drawing.
static void showNonKicking() {
	printf("The vertex that does not kick:\n");

	GS::resetDrawing();
	GS::clear(0x11111111);

	GS::Writes writes;
	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
	writes.add(GS::REG_RGBAQ, GS::rgbaq(0xFF, 0xFF, 0, 0x80));
	writes.add(GS::REG_XYZ3, GS::xyz(2, 2, 0));
	writes.add(GS::REG_XYZ3, GS::xyz(10, 6, 0));
	writes.send();

	GS::readFrame(frame);
	GS::printDistinct("two non-kicking", frame);

	writes.add(GS::REG_XYZ2, GS::xyz(2, 2, 0));
	writes.add(GS::REG_XYZ2, GS::xyz(10, 6, 0));
	writes.send();
	GS::readFrame(frame);
	GS::printDistinct("then two kicking", frame);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, GS::probeWidth * GS::probeHeight * 4);

	GS::reportFrameAddress();
	showTypes();
	showEdges();
	showSubpixel();
	showFlags();
	showModeControl();
	showNonKicking();

	printf("-- TEST END\n");
	return 0;
}
