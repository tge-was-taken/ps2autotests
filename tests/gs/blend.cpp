#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "drawprobe.h"

// The blend, which is (A - B) * C + D with each term chosen from a two bit
// field.  Both operands are known, so each combination's answer is arithmetic
// rather than a picture.

static u32 *frame = 0;

static u64 alpha(u32 a, u32 b, u32 c, u32 d, u32 fix) {
	return (u64)a | ((u64)b << 2) | ((u64)c << 4) | ((u64)d << 6) |
	       ((u64)fix << 32);
}

static void drawOver(u32 background, u32 source, u64 blend, u32 clamp) {
	GS::resetDrawing();
	GS::clear(background);

	GS::Writes writes;
	writes.add(GS::REG_COLCLAMP, clamp);
	writes.add(GS::REG_ALPHA_1, blend);
	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0x40));
	writes.add(GS::REG_RGBAQ, source);
	writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
	writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
	writes.send();

	GS::readFrame(frame);
}

// Every combination of the four fields, on one pair of colours.
static void testEveryCombination() {
	const u32 background = GS::rgbaq(0x40, 0x40, 0x40, 0x20);
	const u32 source = GS::rgbaq(0x80, 0x60, 0x20, 0x60);

	printf("Every blend combination on %08x over %08x:\n", source, background);
	for (u32 a = 0; a < 4; ++a) {
		for (u32 b = 0; b < 4; ++b) {
			for (u32 c = 0; c < 4; ++c) {
				for (u32 d = 0; d < 4; ++d) {
					drawOver(background, source, alpha(a, b, c, d, 0x80), 1);
					printf("  %d%d%d%d: %08x\n", a, b, c, d,
					       GS::pixelAt(frame, 4, 4));
				}
			}
		}
	}
}

// The fixed term, which stands in for an alpha when the third field selects it.
static void testFixedAlpha() {
	static const u32 fixes[] = {0x00, 0x01, 0x40, 0x7F, 0x80, 0x81, 0xFF};
	const u32 background = GS::rgbaq(0x00, 0x00, 0x00, 0x00);
	const u32 source = GS::rgbaq(0xFF, 0x80, 0x40, 0x80);

	printf("The fixed term, blending %08x over black:\n", source);
	for (unsigned i = 0; i < sizeof(fixes) / sizeof(fixes[0]); ++i) {
		drawOver(background, source, alpha(0, 1, 2, 1, fixes[i]), 1);
		printf("  fix %02x: %08x\n", fixes[i], GS::pixelAt(frame, 4, 4));
	}
}

// A source alpha of 0x80, which the unit reads as one rather than as 128/255.
static void testAlphaScale() {
	static const u32 alphas[] = {0x00, 0x01, 0x3F, 0x40, 0x7F, 0x80, 0x81, 0xC0,
	                             0xFF};
	const u32 background = GS::rgbaq(0x00, 0x00, 0x00, 0x00);

	printf("Each source alpha, blending white over black:\n");
	for (unsigned i = 0; i < sizeof(alphas) / sizeof(alphas[0]); ++i) {
		drawOver(background, GS::rgbaq(0xFF, 0xFF, 0xFF, alphas[i]),
		         alpha(0, 1, 0, 1, 0x80), 1);
		printf("  alpha %02x: %08x\n", alphas[i], GS::pixelAt(frame, 4, 4));
	}
}

// The clamp, which decides whether a result outside the range wraps or sticks.
static void testClamp() {
	static const u32 sources[] = {0x80, 0xC0, 0xFF};

	printf("A sum that runs past the top, with and without the clamp:\n");
	for (unsigned i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
		for (u32 clamp = 0; clamp < 2; ++clamp) {
			drawOver(GS::rgbaq(0xC0, 0xC0, 0xC0, 0x80),
			         GS::rgbaq(sources[i], sources[i], sources[i], 0x80),
			         alpha(0, 2, 2, 1, 0x80), clamp);
			printf("  source %02x, clamp %d: %08x\n", sources[i], clamp,
			       GS::pixelAt(frame, 4, 4));
		}
	}

	printf("A difference that runs past the bottom:\n");
	for (u32 clamp = 0; clamp < 2; ++clamp) {
		drawOver(GS::rgbaq(0x20, 0x20, 0x20, 0x80),
		         GS::rgbaq(0x10, 0x10, 0x10, 0x80), alpha(1, 0, 2, 2, 0xFF), clamp);
		printf("  clamp %d: %08x\n", clamp, GS::pixelAt(frame, 4, 4));
	}
}

// The bit that lets a per-pixel alpha turn the blend off, which is what a
// program uses to draw a sprite with holes.
static void testPerPixelDisable() {
	static const u32 alphas[] = {0x00, 0x7F, 0x80, 0xFF};

	printf("The per pixel blend switch:\n");
	for (u32 enabled = 0; enabled < 2; ++enabled) {
		for (unsigned i = 0; i < sizeof(alphas) / sizeof(alphas[0]); ++i) {
			GS::resetDrawing();
			GS::clear(GS::rgbaq(0x40, 0x40, 0x40, 0x80));

			GS::Writes writes;
			writes.add(GS::REG_PABE, enabled);
			writes.add(GS::REG_ALPHA_1, alpha(0, 1, 0, 1, 0x80));
			writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0x40));
			writes.add(GS::REG_RGBAQ, GS::rgbaq(0xFF, 0, 0, alphas[i]));
			writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
			writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
			writes.send();

			GS::readFrame(frame);
			printf("  switch %d alpha %02x: %08x\n", enabled, alphas[i],
			       GS::pixelAt(frame, 4, 4));
		}
	}
	GS::write(GS::REG_PABE, 0);
}

// The bit that forces the alpha written to the buffer, which is separate from
// the one the blend used.
static void testFixedOutputAlpha() {
	printf("The forced output alpha:\n");
	for (u32 forced = 0; forced < 2; ++forced) {
		GS::resetDrawing();
		GS::clear(GS::rgbaq(0, 0, 0, 0));

		GS::Writes writes;
		writes.add(GS::REG_FBA_1, forced);
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
		writes.add(GS::REG_RGBAQ, GS::rgbaq(0x10, 0x20, 0x30, 0x00));
		writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
		writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
		writes.send();

		GS::readFrame(frame);
		printf("  forced %d: %08x\n", forced, GS::pixelAt(frame, 4, 4));
	}
	GS::write(GS::REG_FBA_1, 0);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, GS::probeWidth * GS::probeHeight * 4);

	testAlphaScale();
	testFixedAlpha();
	testClamp();
	testPerPixelDisable();
	testFixedOutputAlpha();
	testEveryCombination();

	printf("-- TEST END\n");
	return 0;
}
