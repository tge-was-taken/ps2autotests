#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "../dma/dmaregs.h"
#include "../dma/dmasend.h"
#include "drawprobe.h"

// Texturing.  A small texture with known values is uploaded, then read back
// through a sprite, so the sampling and the colour function are measured from
// the pixels that come out.

static u32 *frame = 0;
static u32 *texels = 0;

static const int textureWidth = 8;
static const int textureHeight = 8;

// The texture is a gradient across x and y, so a wrong coordinate lands on a
// value that names where it came from.
static void buildTexture() {
	for (int y = 0; y < textureHeight; ++y) {
		for (int x = 0; x < textureWidth; ++x) {
			texels[y * textureWidth + x] =
				(u32)(0x80 << 24) | ((u32)(y * 0x20) << 16) |
				((u32)(x * 0x20) << 8) | (u32)(y * 8 + x);
		}
	}
	SyncDCache(texels, texels + textureWidth * textureHeight);
}

static void upload() {
	GIF::PackedPacket packet(2048);

	GIF::Tag setup;
	setup.SetLoops(4);
	setup.SetEop(false);
	setup.SetRegDescs(GIF::REG_AD);
	packet.WriteTag(setup);
	packet.AD(GS::REG_BITBLTBUF, ((u64)GS::textureBase << 32) | (1ULL << 48));
	packet.AD(GS::REG_TRXPOS, 0);
	packet.AD(GS::REG_TRXREG, (u64)textureWidth | ((u64)textureHeight << 32));
	packet.AD(GS::REG_TRXDIR, 0);

	const u32 quadwords = textureWidth * textureHeight / 4;
	GIF::Tag image;
	image.SetLoops(quadwords);
	image.SetEop();
	image.SetFormat(GIF::FORMAT_IMAGE);
	packet.Packet::WriteTag(image);
	for (u32 i = 0; i < quadwords * 2; ++i) {
		packet.Emit(((u64 *)texels)[i]);
	}

	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	DMA::D2->madr = packet.Raw();
	DMA::D2->qwc = 6 + quadwords;
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_STR | DMA::CHCR_DIR);
	int spins = 1000000;
	while (--spins > 0 && DMA::D2->chcr.Ongoing()) {
		continue;
	}
	DMA::D2->chcr = (DMA::ChannelRegCHCRBits)0;
	GS::write(GS::REG_TEXFLUSH, 0);
}

static u64 tex0(u32 base, u32 width, u32 format, u32 w, u32 h, u32 alpha,
                u32 function) {
	return (u64)base | ((u64)width << 14) | ((u64)format << 20) |
	       ((u64)w << 26) | ((u64)h << 30) | ((u64)alpha << 34) |
	       ((u64)function << 35);
}

// A sprite the size of the buffer, with the texture stretched across it by the
// pixel coordinates rather than by the floating ones.
static void drawTextured(u64 texture, u64 sampling, u32 colour, int u0, int v0,
                         int u1, int v1) {
	GS::resetDrawing();
	GS::clear(0x11111111);

	GS::Writes writes;
	writes.add(GS::REG_TEX0_1, texture);
	writes.add(GS::REG_TEX1_1, sampling);
	writes.add(GS::REG_CLAMP_1, 0);
	writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0x06));
	writes.add(GS::REG_RGBAQ, colour);
	writes.add(GS::REG_UV, (u64)(u0 << 4) | ((u64)(v0 << 4) << 16));
	writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
	writes.add(GS::REG_UV, (u64)(u1 << 4) | ((u64)(v1 << 4) << 16));
	writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
	writes.send();

	GS::readFrame(frame);
}

// The four colour functions, which decide how the texel and the vertex colour
// are combined.
static void testFunctions() {
	static const char *const names[] = {"modulate", "decal   ", "highlight",
	                                    "highlight2"};

	printf("Each colour function, white vertex:\n");
	for (u32 function = 0; function < 4; ++function) {
		drawTextured(tex0(GS::textureBase, 1, 0, 3, 3, 1, function), 0,
		             GS::rgbaq(0x80, 0x80, 0x80, 0x80), 0, 0, 8, 8);
		printf("  %-10s corners %08x %08x %08x %08x\n", names[function],
		       GS::pixelAt(frame, 0, 0), GS::pixelAt(frame, 15, 0),
		       GS::pixelAt(frame, 0, 7), GS::pixelAt(frame, 15, 7));
	}

	printf("Each colour function, half brightness vertex:\n");
	for (u32 function = 0; function < 4; ++function) {
		drawTextured(tex0(GS::textureBase, 1, 0, 3, 3, 1, function), 0,
		             GS::rgbaq(0x40, 0x40, 0x40, 0x40), 0, 0, 8, 8);
		printf("  %-10s middle %08x\n", names[function], GS::pixelAt(frame, 4, 4));
	}
}

// Whether the texture's own alpha is used or taken as one.
static void testAlphaSource() {
	printf("The texture alpha switch:\n");
	for (u32 useAlpha = 0; useAlpha < 2; ++useAlpha) {
		drawTextured(tex0(GS::textureBase, 1, 0, 3, 3, useAlpha, 0), 0,
		             GS::rgbaq(0x80, 0x80, 0x80, 0x80), 0, 0, 8, 8);
		printf("  switch %d: %08x\n", useAlpha, GS::pixelAt(frame, 4, 4));
	}
}

// The sampling, which is either the nearest texel or a mix of four.
static void testSampling() {
	static const char *const names[] = {"nearest", "linear "};

	printf("Each sampling, the texture stretched to twice its size:\n");
	for (u32 sampling = 0; sampling < 2; ++sampling) {
		drawTextured(tex0(GS::textureBase, 1, 0, 3, 3, 1, 0),
		             (u64)sampling << 5 | ((u64)sampling << 6),
		             GS::rgbaq(0x80, 0x80, 0x80, 0x80), 0, 0, 8, 8);
		printf("  %s row 0:", names[sampling]);
		for (int x = 0; x < 8; ++x) {
			printf(" %08x", GS::pixelAt(frame, x, 0));
		}
		printf("\n");
	}
}

// Coordinates outside the texture, which the wrap mode decides the answer for.
static void testWrapping() {
	static const char *const names[] = {"repeat", "clamp ", "region clamp",
	                                    "region repeat"};

	printf("Each wrap mode, coordinates running to twice the width:\n");
	for (u32 mode = 0; mode < 4; ++mode) {
		drawTextured(tex0(GS::textureBase, 1, 0, 3, 3, 1, 0), 0,
		             GS::rgbaq(0x80, 0x80, 0x80, 0x80), 0, 0, 16, 16);
		GS::write(GS::REG_CLAMP_1, (u64)mode | ((u64)mode << 2));

		GS::Writes writes;
		writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0x06));
		writes.add(GS::REG_UV, 0);
		writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
		writes.add(GS::REG_UV, (u64)(16 << 4) | ((u64)(16 << 4) << 16));
		writes.add(GS::REG_XYZ2, GS::xyz(16, 8, 0));
		writes.send();
		GS::readFrame(frame);

		printf("  %-14s row 0:", names[mode]);
		for (int x = 0; x < 8; ++x) {
			printf(" %08x", GS::pixelAt(frame, x, 0));
		}
		printf("\n");
	}
}

// The size fields, which are the base two logarithm of the width and height.
static void testSizes() {
	static const u32 sizes[] = {0, 1, 2, 3, 4, 10};

	printf("Each size field, on an eight by eight texture:\n");
	for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		drawTextured(tex0(GS::textureBase, 1, 0, sizes[i], sizes[i], 1, 0), 0,
		             GS::rgbaq(0x80, 0x80, 0x80, 0x80), 0, 0, 8, 8);
		printf("  size %2d: row 0 %08x %08x %08x %08x\n", sizes[i],
		       GS::pixelAt(frame, 0, 0), GS::pixelAt(frame, 1, 0),
		       GS::pixelAt(frame, 4, 0), GS::pixelAt(frame, 8, 0));
	}
}

// The texel coordinate itself, one pixel at a time, so the whole texture is
// read back through the sampler.
static void testEachTexel() {
	printf("Each texel read through a one pixel sprite:\n");
	for (int v = 0; v < 4; ++v) {
		printf("  row %d:", v);
		for (int u = 0; u < 8; ++u) {
			GS::resetDrawing();
			GS::clear(0x11111111);

			GS::Writes writes;
			writes.add(GS::REG_TEX0_1, tex0(GS::textureBase, 1, 0, 3, 3, 1, 0));
			writes.add(GS::REG_TEX1_1, 0);
			writes.add(GS::REG_CLAMP_1, 0);
			writes.add(GS::REG_PRIM, GS::prim(GS::PRIM_SPRITE, 0x06));
			writes.add(GS::REG_RGBAQ, GS::rgbaq(0x80, 0x80, 0x80, 0x80));
			writes.add(GS::REG_UV, (u64)(u << 4) | ((u64)(v << 4) << 16));
			writes.add(GS::REG_XYZ2, GS::xyz(0, 0, 0));
			writes.add(GS::REG_UV, (u64)((u + 1) << 4) | ((u64)((v + 1) << 4) << 16));
			writes.add(GS::REG_XYZ2, GS::xyz(1, 1, 0));
			writes.send();

			GS::readFrame(frame);
			printf(" %08x", GS::pixelAt(frame, 0, 0));
		}
		printf("\n");
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	frame = (u32 *)memalign(64, GS::probeWidth * GS::probeHeight * 4);
	texels = (u32 *)memalign(64, textureWidth * textureHeight * 4);

	buildTexture();
	upload();

	testEachTexel();
	testFunctions();
	testAlphaSource();
	testSampling();
	testSizes();
	testWrapping();

	printf("-- TEST END\n");
	return 0;
}
