#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "drawprobe.h"

// The depth test: which comparison lets a pixel through, what each depth
// format can hold, and what the write mask stops.

static u32 *frame = 0;

static u64 test(u32 alphaEnable, u32 alphaMethod, u32 reference, u32 fail,
                u32 destEnable, u32 destMethod, u32 depthEnable, u32 depthMethod) {
	return (u64)alphaEnable | ((u64)alphaMethod << 1) | ((u64)reference << 4) |
	       ((u64)fail << 12) | ((u64)destEnable << 14) | ((u64)destMethod << 15) |
	       ((u64)depthEnable << 16) | ((u64)depthMethod << 17);
}

static void setDepth(u32 format, u32 mask) {
	GS::write(GS::REG_ZBUF_1, (u64)GS::zBase | ((u64)format << 24) |
	                              ((u64)mask << 32));
}

// A sprite at one depth over one already drawn at another.
static void drawAtDepth(u32 colour, u32 depth, u32 method) {
	GS::Writes writes;

	writes.add(GS::REG_TEST_1, test(0, 1, 0, 0, 0, 0, 1, method));
	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
	writes.add(GS::REG_RGBAQ, colour);
	writes.add(GS::REG_XYZ2, (u64)(0 << 4) | ((u64)(0 << 4) << 16) |
	                             ((u64)depth << 32));
	writes.add(GS::REG_XYZ2, (u64)(16 << 4) | ((u64)(8 << 4) << 16) |
	                             ((u64)depth << 32));
	writes.send();
}

static void testMethods() {
	static const char *const names[] = {"never  ", "always ", "greater",
	                                    "not less"};

	printf("Each comparison, drawing at depth 100 over depth 200:\n");
	for (u32 method = 0; method < 4; ++method) {
		GS::resetDrawing();
		setDepth(0, 0);
		GS::clear(0x11111111);
		drawAtDepth(GS::rgbaq(0x20, 0x20, 0x20, 0x80), 200, 1);
		drawAtDepth(GS::rgbaq(0xFF, 0, 0, 0x80), 100, method);
		GS::readFrame(frame);

		printf("  %s at 100: %08x\n", names[method], GS::pixelAt(frame, 4, 4));
	}

	printf("Each comparison, drawing at depth 300 over depth 200:\n");
	for (u32 method = 0; method < 4; ++method) {
		GS::resetDrawing();
		setDepth(0, 0);
		GS::clear(0x11111111);
		drawAtDepth(GS::rgbaq(0x20, 0x20, 0x20, 0x80), 200, 1);
		drawAtDepth(GS::rgbaq(0, 0xFF, 0, 0x80), 300, method);
		GS::readFrame(frame);

		printf("  %s at 300: %08x\n", names[method], GS::pixelAt(frame, 4, 4));
	}

	printf("Each comparison, drawing at the same depth:\n");
	for (u32 method = 0; method < 4; ++method) {
		GS::resetDrawing();
		setDepth(0, 0);
		GS::clear(0x11111111);
		drawAtDepth(GS::rgbaq(0x20, 0x20, 0x20, 0x80), 200, 1);
		drawAtDepth(GS::rgbaq(0, 0, 0xFF, 0x80), 200, method);
		GS::readFrame(frame);

		printf("  %s at 200: %08x\n", names[method], GS::pixelAt(frame, 4, 4));
	}
}

// The write mask, which stops the depth being updated without stopping the
// test.
static void testWriteMask() {
	printf("The depth write mask:\n");
	for (u32 mask = 0; mask < 2; ++mask) {
		GS::resetDrawing();
		setDepth(0, mask);
		GS::clear(0x11111111);

		drawAtDepth(GS::rgbaq(0x20, 0x20, 0x20, 0x80), 200, 1);
		// With the mask on, the depth stayed at whatever the clear left, so this
		// draw sees a different value than it would otherwise.
		drawAtDepth(GS::rgbaq(0xFF, 0xFF, 0, 0x80), 150, 2);
		GS::readFrame(frame);

		printf("  mask %d: %08x\n", mask, GS::pixelAt(frame, 4, 4));
	}
}

// The three depth formats, which hold 32, 24 and 16 bits.
static void testFormats() {
	struct Format {
		const char *name;
		u32 value;
		u32 shallow;
		u32 deep;
	};
	static const Format formats[] = {
		{"32 bit", 0x00, 0x0000FFFF, 0xFFFFFFFF},
		{"24 bit", 0x01, 0x0000FFFF, 0x00FFFFFF},
		{"16 bit", 0x02, 0x00001234, 0x0000FFFF},
	};

	printf("Each depth format, drawing deep then shallow:\n");
	for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		GS::resetDrawing();
		setDepth(formats[i].value, 0);
		GS::clear(0x11111111);

		drawAtDepth(GS::rgbaq(0x20, 0x20, 0x20, 0x80), formats[i].shallow, 1);
		drawAtDepth(GS::rgbaq(0xFF, 0, 0xFF, 0x80), formats[i].deep, 2);
		GS::readFrame(frame);
		printf("  %s deep over shallow: %08x\n", formats[i].name,
		       GS::pixelAt(frame, 4, 4));

		GS::resetDrawing();
		setDepth(formats[i].value, 0);
		GS::clear(0x11111111);
		drawAtDepth(GS::rgbaq(0x20, 0x20, 0x20, 0x80), formats[i].deep, 1);
		drawAtDepth(GS::rgbaq(0, 0xFF, 0xFF, 0x80), formats[i].shallow, 2);
		GS::readFrame(frame);
		printf("  %s shallow over deep: %08x\n", formats[i].name,
		       GS::pixelAt(frame, 4, 4));
	}
}

// Depths past what a format can hold, which have to fold rather than carry into
// the comparison.
static void testDepthRange() {
	static const u32 depths[] = {0, 1, 0xFFFF, 0x10000, 0x00FFFFFF, 0x01000000,
	                             0xFFFFFFFF};

	printf("Drawing at each depth over a buffer cleared to depth 0x8000:\n");
	for (unsigned i = 0; i < sizeof(depths) / sizeof(depths[0]); ++i) {
		GS::resetDrawing();
		setDepth(0, 0);
		GS::clear(0x11111111);
		drawAtDepth(GS::rgbaq(0x20, 0x20, 0x20, 0x80), 0x8000, 1);
		drawAtDepth(GS::rgbaq(0xFF, 0xFF, 0xFF, 0x80), depths[i], 2);
		GS::readFrame(frame);

		printf("  depth %08x: %08x\n", depths[i], GS::pixelAt(frame, 4, 4));
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, GS::probeWidth * GS::probeHeight * 4);

	testMethods();
	testWriteMask();
	testFormats();
	testDepthRange();

	printf("-- TEST END\n");
	return 0;
}
