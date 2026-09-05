#ifndef PS2AUTOTESTS_GS_DRAWPROBE_H
#define PS2AUTOTESTS_GS_DRAWPROBE_H

#include <common-ee.h>
#include "emit_giftag.h"

// Drawing into a small buffer and reading the pixels back out, which is what
// every test of the drawing side needs before it can say anything.

namespace GS {

	enum Register {
		REG_PRIM = 0x00,
		REG_RGBAQ = 0x01,
		REG_ST = 0x02,
		REG_UV = 0x03,
		REG_XYZF2 = 0x04,
		REG_XYZ2 = 0x05,
		REG_TEX0_1 = 0x06,
		REG_CLAMP_1 = 0x08,
		REG_FOG = 0x0A,
		REG_XYZ3 = 0x0D,
		REG_TEX1_1 = 0x14,
		REG_XYOFFSET_1 = 0x18,
		REG_XYOFFSET_2 = 0x19,
		REG_PRMODECONT = 0x1A,
		REG_PRMODE = 0x1B,
		REG_TEXCLUT = 0x1C,
		REG_SCANMSK = 0x22,
		REG_TEXA = 0x3B,
		REG_FOGCOL = 0x3D,
		REG_TEXFLUSH = 0x3F,
		REG_SCISSOR_1 = 0x40,
		REG_SCISSOR_2 = 0x41,
		REG_ALPHA_1 = 0x42,
		REG_ALPHA_2 = 0x43,
		REG_DIMX = 0x44,
		REG_DTHE = 0x45,
		REG_COLCLAMP = 0x46,
		REG_TEST_1 = 0x47,
		REG_TEST_2 = 0x48,
		REG_PABE = 0x49,
		REG_FBA_1 = 0x4A,
		REG_FRAME_1 = 0x4C,
		REG_FRAME_2 = 0x4D,
		REG_ZBUF_1 = 0x4E,
		REG_ZBUF_2 = 0x4F,
		REG_BITBLTBUF = 0x50,
		REG_TRXPOS = 0x51,
		REG_TRXREG = 0x52,
		REG_TRXDIR = 0x53,
	};

	enum Primitive {
		PRIM_POINT = 0,
		PRIM_LINE = 1,
		PRIM_LINE_STRIP = 2,
		PRIM_TRIANGLE = 3,
		PRIM_TRIANGLE_STRIP = 4,
		PRIM_TRIANGLE_FAN = 5,
		PRIM_SPRITE = 6,
		PRIM_INVALID = 7,
	};

	// The buffer everything is drawn into: sixteen pixels across and eight down,
	// which is one page and reads back in eight quadwords per row of four.
	static const u32 frameBase = 0x0100;
	static const u32 frameWidth = 1;
	static const u32 zBase = 0x0180;
	static const u32 textureBase = 0x0200;
	static const int probeWidth = 16;
	static const int probeHeight = 8;

	// The unit draws nothing at all with the depth test disabled, so the state
	// every test starts from has it enabled and set to accept everything.
	static const u64 testAlways = (1ULL << 16) | (1ULL << 17);

	// Sets the frame, depth, offset, scissor and test registers to a state every
	// test starts from, so a test only writes what it is about.
	void resetDrawing();

	// Draws one sprite and reads back at every address the frame register could
	// mean, printing where it turned up.  FRAME counts pages and BITBLTBUF
	// counts blocks, and FRAME's field is nine bits wide, so a page number that
	// does not fit is silently something else.
	void reportFrameAddress();

	// A packet of register writes, sent as one.  The entries are held until the
	// send so the tag can be written with the count already known.
	class Writes {
	public:
		Writes();
		void add(u8 address, u64 value);
		void send();

	private:
		static const u32 limit = 64;

		u8 addresses_[limit];
		u64 values_[limit];
		u32 count_;
	};

	void write(u8 address, u64 value);

	// Fills the whole buffer with one colour, using a sprite so the drawing path
	// is the same one under test.
	void clear(u32 colour);

	// A sprite between two corners, drawn with the register state as it stands.
	void drawSprite(int x0, int y0, int x1, int y1, u32 colour);

	// Reads the buffer back.  Returns how many quadwords arrived, which is fewer
	// than asked for when the bus did not turn around.
	int readFrame(u32 *out);

	// The same for a buffer that is not 32 bits a pixel, where the caller says
	// the format and how many quadwords the rectangle comes to.
	int readFrameAs(u32 format, int quadwords, u32 *out);

	// One pixel out of what readFrame returned.
	u32 pixelAt(const u32 *frame, int x, int y);

	// A row of the buffer, and a summary of the whole of it.
	void printRow(const char *what, const u32 *frame, int y);
	void printCorners(const char *what, const u32 *frame);
	void printDistinct(const char *what, const u32 *frame);

	// The scissor holds the two minimums in bits 0 and 32 and the two maximums
	// in bits 16 and 48, so a value built in the obvious order clips everything
	// away.
	u64 scissor(u32 x0, u32 x1, u32 y0, u32 y1);

	u64 rgbaq(u32 r, u32 g, u32 b, u32 a);
	u64 xyz(u32 x, u32 y, u32 z);
	u64 prim(u32 type, u32 flags);
}

#endif
