#include <common-ee.h>
#include <string.h>
#include "ipuregs.h"

// Colour space conversion, which is the one command whose whole answer can be
// read back without a picture to decode.  One macroblock of known luminance and
// colour goes in and the pixels come out, so the coefficients are pinned by the
// numbers rather than by a picture.

// A macroblock is sixteen by sixteen luminance and eight by eight of each
// colour difference, which is twenty four quadwords.
static const int macroblockQuadwords = 24;
static const int pixelQuadwords = 64;

static u8 macroblock[macroblockQuadwords * 16] __attribute__((aligned(16)));
static u32 pixels[pixelQuadwords * 4] __attribute__((aligned(16)));

static void buildFlat(u8 luminance, u8 blue, u8 red) {
	memset(macroblock, luminance, 256);
	memset(macroblock + 256, blue, 64);
	memset(macroblock + 320, red, 64);
}

// A gradient across the luminance, so one macroblock covers the whole range
// rather than one point of it.
static void buildGradient() {
	for (int i = 0; i < 256; ++i) {
		macroblock[i] = (u8)i;
	}
	memset(macroblock + 256, 128, 64);
	memset(macroblock + 320, 128, 64);
}

static void send() {
	const volatile u128 *source = (const volatile u128 *)macroblock;
	for (int i = 0; i < macroblockQuadwords; ++i) {
		*IPU::IN_FIFO = source[i];
	}
}

static int receive() {
	volatile u128 *destination = (volatile u128 *)pixels;
	int received = 0;

	for (int spins = 0; spins < 200000 && received < pixelQuadwords; ++spins) {
		if (IPU::outputCount() == 0) {
			continue;
		}
		destination[received] = *IPU::OUT_FIFO;
		received++;
	}
	return received;
}

static void printPixels(const char *what, int received) {
	printf("  %s: %d quadwords\n", what, received);
	if (received == 0) {
		return;
	}
	static const int samples[] = {0, 1, 15, 16, 128, 240, 255};
	printf("   ");
	for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
		if (samples[i] < received * 4) {
			printf(" %08x", pixels[samples[i]]);
		}
	}
	printf("\n");
}

static int convert(u32 options) {
	IPU::reset();
	memset(pixels, 0, sizeof(pixels));

	send();
	IPU::write((IPU::CMD_CSC << 28) | options | 1);
	return receive();
}

// A flat block of each corner of the colour space.
static void testFlatColours() {
	struct Colour {
		const char *name;
		u8 luminance;
		u8 blue;
		u8 red;
	};
	static const Colour colours[] = {
		{"black    ", 0x00, 0x80, 0x80},
		{"grey     ", 0x80, 0x80, 0x80},
		{"white    ", 0xFF, 0x80, 0x80},
		{"red      ", 0x80, 0x80, 0xFF},
		{"blue     ", 0x80, 0xFF, 0x80},
		{"green    ", 0x80, 0x00, 0x00},
		{"below    ", 0x00, 0x00, 0x00},
		{"above    ", 0xFF, 0xFF, 0xFF},
		{"broadcast", 0x10, 0x80, 0x80},
	};

	printf("A flat macroblock of each colour:\n");
	for (unsigned i = 0; i < sizeof(colours) / sizeof(colours[0]); ++i) {
		buildFlat(colours[i].luminance, colours[i].blue, colours[i].red);
		const int received = convert(0);
		printPixels(colours[i].name, received);
	}
	IPU::reset();
}

// The same block with the two option bits, which pick the output width and
// whether the unit dithers.
static void testOptions() {
	struct Option {
		const char *name;
		u32 bits;
	};
	static const Option options[] = {
		{"32-bit, no dither", 0},
		{"32-bit, dither   ", 1u << 26},
		{"16-bit, no dither", 1u << 27},
		{"16-bit, dither   ", (1u << 27) | (1u << 26)},
	};

	printf("A grey macroblock through each option:\n");
	buildFlat(0x80, 0x80, 0x80);
	for (unsigned i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
		const int received = convert(options[i].bits);
		printPixels(options[i].name, received);
	}
	IPU::reset();
}

// A gradient, so the whole luminance range is covered by one block.
static void testGradient() {
	printf("A macroblock with a luminance gradient:\n");

	buildGradient();
	const int received = convert(0);
	printf("  %d quadwords\n", received);
	for (int i = 0; i < 8 && i * 32 < received * 4; ++i) {
		printf("   %3d: %08x %08x %08x %08x\n", i * 32, pixels[i * 32],
		       pixels[i * 32 + 1], pixels[i * 32 + 2], pixels[i * 32 + 3]);
	}
	IPU::reset();
}

// More than one macroblock in a single command, which is the count field.
static void testMacroblockCount() {
	printf("The macroblock count:\n");

	for (u32 count = 1; count <= 3; ++count) {
		IPU::reset();
		memset(pixels, 0, sizeof(pixels));

		buildFlat(0x80, 0x80, 0x80);
		for (u32 i = 0; i < count; ++i) {
			send();
		}
		IPU::write((IPU::CMD_CSC << 28) | count);

		int received = 0;
		volatile u128 *destination = (volatile u128 *)pixels;
		for (int spins = 0; spins < 400000 && received < pixelQuadwords; ++spins) {
			if (IPU::outputCount() == 0) {
				continue;
			}
			destination[received] = *IPU::OUT_FIFO;
			received++;
		}
		printf("  count %u: %d of the first block's quadwords, ctrl %08x\n", count,
		       received, *IPU::CTRL);
	}
	IPU::reset();
}

// A command with no data behind it, so the record says whether the unit waits
// or answers with nothing.
static void testWithNoInput() {
	printf("A conversion with nothing in the input:\n");

	IPU::reset();
	IPU::write((IPU::CMD_CSC << 28) | 1);
	printf("  idle %s, ctrl %08x, out %d\n", IPU::waitForIdle() ? "yes" : "no ",
	       *IPU::CTRL, IPU::outputCount());

	printf("  feeding the block now\n");
	buildFlat(0x80, 0x80, 0x80);
	send();
	const int received = receive();
	printPixels("after feeding", received);

	IPU::reset();
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testFlatColours();
	testOptions();
	testGradient();
	testMacroblockCount();
	testWithNoInput();

	printf("-- TEST END\n");
	return 0;
}
