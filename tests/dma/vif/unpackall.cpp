#include <common-ee.h>
#include <kernel.h>
#include <string.h>
#include "../dmaregs.h"
#include "../dmasend.h"
#include "emit_vifcode.h"
#include "vifunits.h"

// Every unpack format against the mask, the write cycle and the add modes.
// The existing unpack test covers one value per format; this one is about the
// combinations around it.

struct Format {
	const char *name;
	VIF::UnpackType type;
	int wordsPerElement;
};

static const Format formats[] = {
	{"S32",   VIF::UNPACK_TYPE_S32,       1},
	{"S16",   VIF::UNPACK_TYPE_S16,       1},
	{"S8",    VIF::UNPACK_TYPE_S8,        1},
	{"V2-32", VIF::UNPACK_TYPE_V2_32,     2},
	{"V2-16", VIF::UNPACK_TYPE_V2_16,     1},
	{"V2-8",  VIF::UNPACK_TYPE_V2_8,      1},
	{"V3-32", VIF::UNPACK_TYPE_V3_32,     3},
	{"V3-16", VIF::UNPACK_TYPE_V3_16,     2},
	{"V3-8",  VIF::UNPACK_TYPE_V3_8,      1},
	{"V4-32", VIF::UNPACK_TYPE_V4_32,     4},
	{"V4-16", VIF::UNPACK_TYPE_V4_16,     2},
	{"V4-8",  VIF::UNPACK_TYPE_V4_8,      1},
	{"V4-5",  VIF::UNPACK_TYPE_V4_5_5_5_1, 1},
};

static const int formatCount = sizeof(formats) / sizeof(formats[0]);

// A pattern whose every byte differs, so a narrow format shows which bytes it
// took and in what order.
static const u32 payload[4] = {0x84838281, 0x88878685, 0x8C8B8A89, 0x908F8E8D};

static void clearTarget(VIF::Unit *unit, int quadwords) {
	memset(unit->vuMem, 0xFF, quadwords * 16);
	SyncDCache(unit->vuMem, unit->vuMem + quadwords * 16);
}

static void printTarget(VIF::Unit *unit, int quadwords) {
	SyncDCache(unit->vuMem, unit->vuMem + quadwords * 16);
	const volatile u32 *words = (const volatile u32 *)unit->vuMem;
	for (int q = 0; q < quadwords; ++q) {
		printf(" %08x %08x %08x %08x", words[q * 4], words[q * 4 + 1],
		       words[q * 4 + 2], words[q * 4 + 3]);
	}
}

static void unpack(VIF::Unit *unit, const Format &format, u16 count, u16 address,
                   VIF::UnpackFlags flags, u8 cl, u8 wl, int dataWords) {
	unit->regs->fbrst = VIF::FBRST_RST;

	VIF::Packet packet(256);
	packet.STCYCL(cl, wl);
	packet.UNPACK(format.type, count, address, flags);
	for (int i = 0; i < dataWords; ++i) {
		packet.Data32(payload[i % 4]);
	}
	for (int i = dataWords; i < 12; ++i) {
		packet.Data32(0);
	}
	DMA::SendSimple(unit->dmaChannel, packet.Raw(), 64);
}

static void testFormats(VIF::Unit *unit, const char *name) {
	printf("%s each format, two elements:\n", name);
	for (int f = 0; f < formatCount; ++f) {
		for (int usn = 0; usn < 2; ++usn) {
			clearTarget(unit, 2);
			unpack(unit, formats[f], 2, 0,
			       usn ? VIF::UNPACK_ZERO_EXTEND : VIF::UNPACK_NORMAL, 1, 1, 4);

			printf("  %-6s %s:", formats[f].name, usn ? "usn" : "sgn");
			printTarget(unit, 2);
			printf("\n");
		}
	}
}

// The mask register replaces a lane with the row value, the column value or
// what was already there.
static void testMasks(VIF::Unit *unit, const char *name) {
	static const u32 masks[] = {
		0x00000000, 0xFFFFFFFF, 0x55555555, 0xAAAAAAAA, 0x000000FF, 0x0000FF00,
	};

	printf("%s V4-32 with a mask:\n", name);
	for (unsigned m = 0; m < sizeof(masks) / sizeof(masks[0]); ++m) {
		unit->regs->fbrst = VIF::FBRST_RST;
		clearTarget(unit, 2);

		VIF::Packet packet(256);
		packet.STROW(0x11111111, 0x22222222, 0x33333333, 0x44444444);
		packet.STCOL(0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD);
		packet.STMASK(masks[m]);
		packet.STCYCL(1, 1);
		packet.UNPACK(VIF::UNPACK_TYPE_V4_32, 2, 0, VIF::UNPACK_ENABLE_MASKS);
		for (int i = 0; i < 8; ++i) {
			packet.Data32(payload[i % 4]);
		}
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 96);

		printf("  %08x:", masks[m]);
		printTarget(unit, 2);
		printf("\n");
	}
}

// The three add modes: write, add the row, or add the row to a running total.
static void testModes(VIF::Unit *unit, const char *name) {
	printf("%s V4-32 in each add mode:\n", name);
	for (u8 mode = 0; mode < 4; ++mode) {
		unit->regs->fbrst = VIF::FBRST_RST;
		clearTarget(unit, 2);

		VIF::Packet packet(256);
		packet.STROW(0x00000001, 0x00000002, 0x00000003, 0x00000004);
		packet.STMOD(mode);
		packet.STCYCL(1, 1);
		packet.UNPACK(VIF::UNPACK_TYPE_V4_32, 2, 0, VIF::UNPACK_NORMAL);
		for (int i = 0; i < 8; ++i) {
			packet.Data32(0x00000010);
		}
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 80);

		printf("  mode %d:", mode);
		printTarget(unit, 2);
		printf("\n");
	}
	unit->regs->fbrst = VIF::FBRST_RST;
}

// A write cycle wider than the read cycle leaves gaps, and the other way round
// packs them.
static void testCycles(VIF::Unit *unit, const char *name) {
	static const u8 cycles[][2] = {{1, 1}, {2, 1}, {1, 2}, {2, 2}, {4, 1}, {1, 4},
	                               {3, 2}, {0, 1}, {1, 0}};

	printf("%s V4-32 at each write cycle:\n", name);
	for (unsigned c = 0; c < sizeof(cycles) / sizeof(cycles[0]); ++c) {
		clearTarget(unit, 4);
		unpack(unit, formats[9], 4, 0, VIF::UNPACK_NORMAL, cycles[c][0], cycles[c][1],
		       12);

		printf("  cl %d wl %d:", cycles[c][0], cycles[c][1]);
		printTarget(unit, 4);
		printf("\n");
	}
	unit->regs->fbrst = VIF::FBRST_RST;
}

// An address near the end of vu memory, and the flag that adds the buffer base
// to it.
static void testAddresses(VIF::Unit *unit, const char *name) {
	static const u16 addresses[] = {0, 1, 2, 0x3FE, 0x3FF};

	printf("%s V4-32 at an address:\n", name);
	for (unsigned a = 0; a < sizeof(addresses) / sizeof(addresses[0]); ++a) {
		unit->regs->fbrst = VIF::FBRST_RST;
		memset(unit->vuMem, 0xFF, 0x40);
		SyncDCache(unit->vuMem, unit->vuMem + 0x40);

		unpack(unit, formats[9], 1, addresses[a], VIF::UNPACK_NORMAL, 1, 1, 4);

		const volatile u32 *words = (const volatile u32 *)unit->vuMem;
		SyncDCache(unit->vuMem, unit->vuMem + 0x40);
		printf("  addr %04x: first quadword %08x %08x %08x %08x, num %02x\n",
		       addresses[a], words[0], words[1], words[2], words[3],
		       unit->regs->num & 0xFF);
	}
}

// A count of zero, which the eight bit field spells the same as 256.
static void testCounts(VIF::Unit *unit, const char *name) {
	static const u16 counts[] = {1, 2, 255, 256};

	printf("%s V4-8 element counts:\n", name);
	for (unsigned c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c) {
		unit->regs->fbrst = VIF::FBRST_RST;
		clearTarget(unit, 4);

		VIF::Packet packet(1024);
		packet.STCYCL(1, 1);
		packet.UNPACK(VIF::UNPACK_TYPE_V4_8, counts[c], 0, VIF::UNPACK_NORMAL);
		for (u16 i = 0; i < counts[c]; ++i) {
			packet.Data32(payload[i % 4]);
		}
		const u32 bytes = (8 + counts[c] * 4 + 15) & ~15u;
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), bytes);

		printf("  %3u:", counts[c]);
		printTarget(unit, 2);
		printf(" num %02x\n", unit->regs->num & 0xFF);
	}
}

static void doTest(VIF::Unit *unit, const char *name) {
	testFormats(unit, name);
	testMasks(unit, name);
	testModes(unit, name);
	testCycles(unit, name);
	testAddresses(unit, name);
	testCounts(unit, name);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	doTest(&VIF::Unit0, "VIF0");
	doTest(&VIF::Unit1, "VIF1");

	printf("-- TEST END\n");
	return 0;
}
