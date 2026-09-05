#include <common-ee.h>
#include <string.h>
#include "ipuregs.h"

// The bit stream the unit reads its input from: where the position starts, what
// a fixed length read returns, and how the position moves.  Every value in the
// input is chosen so the bits read back name their own offset.

static void fill(int quadwords) {
	for (int i = 0; i < quadwords; ++i) {
		u32 quad[4];
		quad[0] = 0x01234567 + (i << 24);
		quad[1] = 0x89ABCDEF;
		quad[2] = 0xFEDCBA98 - (i << 24);
		quad[3] = 0x76543210;
		*IPU::IN_FIFO = *(volatile u128 *)quad;
	}
}

static void printPosition(const char *what) {
	printf("  %-28s bp %08x ctrl %08x cmd %016llx\n", what, *IPU::BP, *IPU::CTRL,
	       *IPU::CMD);
}

// Where the position sits with nothing having been read.
static void testStartingPosition() {
	printf("The position on an empty unit:\n");

	IPU::reset();
	printPosition("after a reset");

	fill(4);
	printPosition("with four quadwords in");
}

// The clear command, which is how a program moves the position by hand.
static void testClearPositions() {
	static const u32 positions[] = {0, 1, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127,
	                                128, 255};

	printf("The position after a clear:\n");
	for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
		IPU::reset();
		fill(4);
		IPU::write((IPU::CMD_BCLR << 28) | positions[i]);
		IPU::waitForIdle();

		char name[36];
		sprintf(name, "cleared to %3u", positions[i]);
		printPosition(name);
	}
}

// A fixed length read at each starting position, so the bits it returns say
// which end of the quadword the stream starts from.
static void testFixedRead() {
	static const u32 positions[] = {0, 1, 4, 8, 12, 16, 24, 31, 32, 48, 64, 96,
	                                120, 127};

	printf("A fixed length read from each position:\n");
	for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
		IPU::reset();
		fill(4);
		IPU::write((IPU::CMD_BCLR << 28) | positions[i]);
		IPU::waitForIdle();

		IPU::write(IPU::CMD_FDEC << 28);
		const bool idle = IPU::waitForIdle();

		printf("  from %3u: idle %s, cmd %016llx, bp now %08x\n", positions[i],
		       idle ? "yes" : "no ", *IPU::CMD, *IPU::BP);
	}
	IPU::reset();
}

// Two reads in a row, which says whether the first one moved the position.
static void testConsecutiveReads() {
	printf("Four reads in a row from zero:\n");

	IPU::reset();
	fill(8);
	IPU::write((IPU::CMD_BCLR << 28) | 0);
	IPU::waitForIdle();

	for (int i = 0; i < 4; ++i) {
		IPU::write(IPU::CMD_FDEC << 28);
		const bool idle = IPU::waitForIdle();
		printf("  read %d: idle %s, cmd %016llx, bp %08x, in %d\n", i,
		       idle ? "yes" : "no ", *IPU::CMD, *IPU::BP, IPU::inputCount());
	}
	IPU::reset();
}

// A read that reaches past what the input holds, which is where the unit has
// to wait for more rather than return what it has.
static void testReadPastTheEnd() {
	printf("Reading past what the input holds:\n");

	IPU::reset();
	fill(1);
	IPU::write((IPU::CMD_BCLR << 28) | 0);
	IPU::waitForIdle();

	for (int i = 0; i < 6; ++i) {
		IPU::write(IPU::CMD_FDEC << 28);
		const bool idle = IPU::waitForIdle();
		printf("  read %d: idle %s, cmd %016llx, bp %08x, in %d\n", i,
		       idle ? "yes" : "no ", *IPU::CMD, *IPU::BP, IPU::inputCount());
		if (!idle) {
			printf("  feeding one more quadword\n");
			fill(1);
			printf("  now idle %s, cmd %016llx, bp %08x\n",
			       IPU::waitForIdle() ? "yes" : "no ", *IPU::CMD, *IPU::BP);
			break;
		}
	}
	IPU::reset();
}

// The variable length read, whose table depends on the picture state the unit
// has not been given, so what is recorded is that it consumes rather than what
// it decodes.
static void testVariableRead() {
	static const u32 tables[] = {0, 1, 2, 3, 4, 5, 6, 7};

	printf("A variable length read with each table:\n");
	for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); ++i) {
		IPU::reset();
		fill(8);
		IPU::write((IPU::CMD_BCLR << 28) | 0);
		IPU::waitForIdle();

		IPU::write((IPU::CMD_VDEC << 28) | (tables[i] << 26));
		const bool idle = IPU::waitForIdle();
		printf("  table %u: idle %s, cmd %016llx, bp %08x, top %08x\n", tables[i],
		       idle ? "yes" : "no ", *IPU::CMD, *IPU::BP, *IPU::TOP);
	}
	IPU::reset();
}

// The peek register, which holds the next bits without taking them.
static void testTopRegister() {
	printf("The peek register as the position moves:\n");

	IPU::reset();
	fill(4);
	for (u32 position = 0; position <= 40; position += 8) {
		IPU::write((IPU::CMD_BCLR << 28) | position);
		IPU::waitForIdle();
		printf("  at %3u: top %08x bp %08x\n", position, *IPU::TOP, *IPU::BP);
	}
	IPU::reset();
}

// The clear command with the input already part read, since it has to drop
// what is in flight rather than leave it.
static void testClearAfterReading() {
	printf("A clear after some of the input has been read:\n");

	IPU::reset();
	fill(4);
	printPosition("filled");

	IPU::write((IPU::CMD_BCLR << 28) | 0);
	IPU::waitForIdle();
	IPU::write(IPU::CMD_FDEC << 28);
	IPU::waitForIdle();
	printPosition("after one read");

	IPU::write((IPU::CMD_BCLR << 28) | 0);
	IPU::waitForIdle();
	printPosition("after a clear to zero");

	IPU::write(IPU::CMD_FDEC << 28);
	IPU::waitForIdle();
	printPosition("after reading again");

	IPU::reset();
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testStartingPosition();
	testClearPositions();
	testTopRegister();
	testFixedRead();
	testConsecutiveReads();
	testClearAfterReading();
	testVariableRead();
	testReadPastTheEnd();

	printf("-- TEST END\n");
	return 0;
}
