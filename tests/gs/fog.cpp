#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "drawprobe.h"

// The fog, which mixes the fog colour into a pixel by an amount carried on the
// vertex, and the dither that goes with a narrow target.

static u32 *frame = 0;

static void drawWithFog(u32 colour, u32 fogColour, u32 amount) {
	GS::resetDrawing();
	GS::clear(0x11111111);

	GS::Writes writes;
	writes.add(GS::REG_FOGCOL, fogColour);
	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0x10));
	writes.add(GS::REG_RGBAQ, colour);
	writes.add(GS::REG_XYZF2, (u64)(0 << 4) | ((u64)(0 << 4) << 16) |
	                              ((u64)amount << 56));
	writes.add(GS::REG_XYZF2, (u64)(16 << 4) | ((u64)(8 << 4) << 16) |
	                              ((u64)amount << 56));
	writes.send();

	GS::readFrame(frame);
}

// Each amount, which is eight bits on the vertex.
static void testAmounts() {
	static const u32 amounts[] = {0x00, 0x01, 0x40, 0x7F, 0x80, 0x81, 0xC0, 0xFE,
	                              0xFF};

	printf("White under a black fog at each amount:\n");
	for (unsigned i = 0; i < sizeof(amounts) / sizeof(amounts[0]); ++i) {
		drawWithFog(GS::rgbaq(0xFF, 0xFF, 0xFF, 0x80), 0, amounts[i]);
		printf("  amount %02x: %08x\n", amounts[i], GS::pixelAt(frame, 4, 4));
	}
}

// The fog colour, which is three bytes and no alpha.
static void testColour() {
	static const u32 colours[] = {0x000000, 0xFFFFFF, 0x0000FF, 0x00FF00,
	                              0xFF0000, 0xFFFFFFFF};

	printf("Grey under each fog colour at half:\n");
	for (unsigned i = 0; i < sizeof(colours) / sizeof(colours[0]); ++i) {
		drawWithFog(GS::rgbaq(0x80, 0x80, 0x80, 0x80), colours[i], 0x80);
		printf("  colour %08x: %08x\n", colours[i], GS::pixelAt(frame, 4, 4));
	}
}

// The separate fog register, which sets the amount for vertices written
// without one.
static void testFogRegister() {
	static const u32 amounts[] = {0x00, 0x40, 0x80, 0xFF};

	printf("The amount taken from the fog register instead:\n");
	for (unsigned i = 0; i < sizeof(amounts) / sizeof(amounts[0]); ++i) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		writes.add(GS::REG_FOGCOL, 0x0000FF);
		writes.add(GS::REG_FOG, (u64)amounts[i] << 56);
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0x10));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0xFF, 0xFF, 0xFF, 0x80));
		writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
		writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
		writes.send();

		GS::readFrame(frame);
		printf("  amount %02x: %08x\n", amounts[i], GS::pixelAt(frame, 4, 4));
	}
}

// The fog flag off, so the amount on the vertex is carried but not used.
static void testFlagOff() {
	printf("With the fog flag off:\n");
	for (u32 flag = 0; flag < 2; ++flag) {
		GS::resetDrawing();
		GS::clear(0x11111111);

		GS::Writes writes;
		writes.add(GS::REG_FOGCOL, 0x0000FF);
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, flag << 4));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0xFF, 0xFF, 0xFF, 0x80));
		writes.add(GS::REG_XYZF2, (u64)(0 << 4) | ((u64)(0 << 4) << 16) |
		                              (0x80ULL << 56));
		writes.add(GS::REG_XYZF2, (u64)(16 << 4) | ((u64)(8 << 4) << 16) |
		                              (0x80ULL << 56));
		writes.send();

		GS::readFrame(frame);
		printf("  flag %d: %08x\n", flag, GS::pixelAt(frame, 4, 4));
	}
}

// The alpha, which the fog is documented as leaving alone.
static void testAlphaUntouched() {
	static const u32 alphas[] = {0x00, 0x40, 0x80, 0xFF};

	printf("The alpha through the fog:\n");
	for (unsigned i = 0; i < sizeof(alphas) / sizeof(alphas[0]); ++i) {
		drawWithFog(GS::rgbaq(0x80, 0x80, 0x80, alphas[i]), 0xFF0000, 0x40);
		printf("  alpha %02x: %08x\n", alphas[i], GS::pixelAt(frame, 4, 4));
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, GS::probeWidth * GS::probeHeight * 4);

	testAmounts();
	testColour();
	testFogRegister();
	testFlagOff();
	testAlphaUntouched();

	printf("-- TEST END\n");
	return 0;
}
