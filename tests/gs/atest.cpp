#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "drawprobe.h"

// The alpha test and the destination alpha test, which share one register with
// the depth test, and what each failure mode still writes.

static u32 *frame = 0;

static u64 test(u32 alphaEnable, u32 alphaMethod, u32 reference, u32 fail,
                u32 destEnable, u32 destMethod, u32 depthEnable, u32 depthMethod) {
	return (u64)alphaEnable | ((u64)alphaMethod << 1) | ((u64)reference << 4) |
	       ((u64)fail << 12) | ((u64)destEnable << 14) | ((u64)destMethod << 15) |
	       ((u64)depthEnable << 16) | ((u64)depthMethod << 17);
}

static void draw(u64 testValue, u32 colour) {
	GS::Writes writes;

	writes.add(GS::REG_TEST_1, testValue);
	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0));
	writes.add(GS::REG_RGBAQ, colour);
	writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
	writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
	writes.send();
}

// Each comparison against a reference of 0x80, with the source alpha swept.
static void testMethods() {
	static const char *const names[] = {"never   ", "always  ", "less    ",
	                                    "less eq ", "equal   ", "greater eq",
	                                    "greater ", "not equal"};
	static const u32 alphas[] = {0x00, 0x7F, 0x80, 0x81, 0xFF};

	printf("Each alpha comparison against 80:\n");
	for (u32 method = 0; method < 8; ++method) {
		printf("  %-10s", names[method]);
		for (unsigned i = 0; i < sizeof(alphas) / sizeof(alphas[0]); ++i) {
			GS::resetDrawing();
			GS::clear(0x11111111);
			draw(test(1, method, 0x80, 0, 0, 0, 1, 1),
			     GS::rgbaq(0xFF, 0, 0, alphas[i]));
			GS::readFrame(frame);
			printf(" %02x:%08x", alphas[i], GS::pixelAt(frame, 4, 4));
		}
		printf("\n");
	}
}

// The reference value, swept against a fixed source alpha.
static void testReference() {
	static const u32 references[] = {0x00, 0x01, 0x7F, 0x80, 0x81, 0xFE, 0xFF};

	printf("Each reference, source alpha 80, greater than:\n");
	for (unsigned i = 0; i < sizeof(references) / sizeof(references[0]); ++i) {
		GS::resetDrawing();
		GS::clear(0x11111111);
		draw(test(1, 6, references[i], 0, 0, 0, 1, 1),
		     GS::rgbaq(0, 0xFF, 0, 0x80));
		GS::readFrame(frame);
		printf("  reference %02x: %08x\n", references[i],
		       GS::pixelAt(frame, 4, 4));
	}
}

// What a failed pixel still writes, which is the field a program uses to fill
// the depth buffer without touching the colour.
static void testFailureModes() {
	static const char *const names[] = {"keep    ", "colour  ", "depth   ",
	                                    "colour, alpha kept"};

	printf("Each failure mode, on a test that always fails:\n");
	for (u32 mode = 0; mode < 4; ++mode) {
		GS::resetDrawing();
		GS::clear(GS::rgbaq(0x11, 0x22, 0x33, 0x44));
		draw(test(1, 0, 0x80, mode, 0, 0, 1, 1),
		     GS::rgbaq(0xAA, 0xBB, 0xCC, 0xDD));
		GS::readFrame(frame);
		printf("  %-18s %08x\n", names[mode], GS::pixelAt(frame, 4, 4));
	}
}

// The destination test, which reads the alpha already in the buffer.
static void testDestination() {
	static const u32 written[] = {0x00, 0x7F, 0x80, 0xFF};

	printf("The destination alpha test:\n");
	for (u32 method = 0; method < 2; ++method) {
		for (unsigned i = 0; i < sizeof(written) / sizeof(written[0]); ++i) {
			GS::resetDrawing();
			GS::clear(GS::rgbaq(0x11, 0x22, 0x33, written[i]));
			draw(test(0, 1, 0, 0, 1, method, 1, 1),
			     GS::rgbaq(0xAA, 0xBB, 0xCC, 0x80));
			GS::readFrame(frame);
			printf("  method %d, buffer alpha %02x: %08x\n", method, written[i],
			       GS::pixelAt(frame, 4, 4));
		}
	}
}

// Both tests at once, so the order they are applied in decides the answer.
static void testBothTests() {
	printf("The alpha and destination tests together:\n");
	for (u32 alphaMethod = 0; alphaMethod < 2; ++alphaMethod) {
		for (u32 destMethod = 0; destMethod < 2; ++destMethod) {
			GS::resetDrawing();
			GS::clear(GS::rgbaq(0x11, 0x22, 0x33, 0x80));
			draw(test(1, alphaMethod, 0x80, 0, 1, destMethod, 1, 1),
			     GS::rgbaq(0xAA, 0xBB, 0xCC, 0x40));
			GS::readFrame(frame);
			printf("  alpha %d dest %d: %08x\n", alphaMethod, destMethod,
			       GS::pixelAt(frame, 4, 4));
		}
	}
}

// Every field of the register written at once, since one field's bits landing
// in another is the failure a wrong layout produces.
static void testRegisterLayout() {
	// The depth enable is bit sixteen, so a value without it draws nothing
	// whatever the rest of the register says.
	static const u64 values[] = {0x00000000, 0x0000FFFF, 0x00030001, 0x00030FF0,
	                             0x00033000, 0x00034000, 0x00010000, 0x00060000};

	printf("The test register written directly:\n");
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		GS::resetDrawing();
		GS::clear(GS::rgbaq(0x11, 0x22, 0x33, 0x80));
		draw(values[i], GS::rgbaq(0xAA, 0xBB, 0xCC, 0x80));
		GS::readFrame(frame);
		printf("  %016llx: %08x\n", values[i], GS::pixelAt(frame, 4, 4));
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, GS::probeWidth * GS::probeHeight * 4);

	testMethods();
	testReference();
	testFailureModes();
	testDestination();
	testBothTests();
	testRegisterLayout();

	printf("-- TEST END\n");
	return 0;
}
