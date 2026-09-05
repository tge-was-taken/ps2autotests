#include <common-ee.h>
#include <kernel.h>
#include <string.h>
#include "../dmaregs.h"
#include "../dmasend.h"
#include "emit_vifcode.h"
#include "vifunits.h"

// Nothing here runs what it uploads, so the payload is a pattern rather than
// code and the test is only about where the bytes land.
static u64 __attribute__((aligned(16))) g_pattern[256];

static u8 *microMemory(VIF::Unit *unit) {
	return unit == &VIF::Unit0 ? (u8 *)0x11000000 : (u8 *)0x11008000;
}

static u32 microSize(VIF::Unit *unit) {
	return unit == &VIF::Unit0 ? 0x1000 : 0x4000;
}

static void fillPattern(u32 instructions, u32 tag) {
	for (u32 i = 0; i < instructions; ++i) {
		g_pattern[i] = ((u64)tag << 48) | i;
	}
}

static void clearMicro(VIF::Unit *unit) {
	u8 *micro = microMemory(unit);
	memset(micro, 0, microSize(unit));
	SyncDCache(micro, micro + microSize(unit));
}

static void printInstruction(VIF::Unit *unit, u32 index) {
	const u64 *micro = (const u64 *)microMemory(unit);
	printf("%016llx", micro[index]);
}

static void upload(VIF::Unit *unit, u16 address, u16 size, u32 instructions) {
	VIF::Packet packet(16 + instructions * 8 + 16);
	packet.MPG(address, size);
	packet.DataPtr(g_pattern, instructions * 8);
	packet.NOP();
	packet.NOP();
	packet.NOP();

	const u32 bytes = (4 + instructions * 8 + 12 + 15) & ~15u;
	DMA::SendSimple(unit->dmaChannel, packet.Raw(), bytes);

	u8 *micro = microMemory(unit);
	SyncDCache(micro, micro + microSize(unit));
}

// Where the payload lands for a given address, and what neighbours it.
static void testAddress(VIF::Unit *unit, const char *name) {
	static const u16 addresses[] = {0, 1, 2, 0x40, 0x1FE, 0x1FF, 0x200, 0x7FE, 0x7FF};

	printf("%s MPG address, four instructions:\n", name);
	for (unsigned a = 0; a < sizeof(addresses) / sizeof(addresses[0]); ++a) {
		const u16 address = addresses[a];
		unit->regs->fbrst = VIF::FBRST_RST;
		clearMicro(unit);
		fillPattern(4, 0x1000 + a);
		upload(unit, address, 4, 4);

		const u32 instructions = microSize(unit) / 8;
		printf("  addr %04x:", address);
		for (u32 i = 0; i < 4; ++i) {
			printf(" ");
			printInstruction(unit, (address + i) % instructions);
		}
		printf("\n");
	}
}

// The size field is eight bits, so 256 has to be spelled as zero.
static void testSize(VIF::Unit *unit, const char *name) {
	static const u16 sizes[] = {1, 2, 8, 255, 256};

	printf("%s MPG size:\n", name);
	for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
		const u16 size = sizes[s];
		unit->regs->fbrst = VIF::FBRST_RST;
		clearMicro(unit);
		fillPattern(size, 0x2000 + s);
		upload(unit, 0, size, size);

		printf("  size %3u: first ", size);
		printInstruction(unit, 0);
		printf(" last ");
		printInstruction(unit, size - 1);
		printf(" next ");
		printInstruction(unit, size);
		printf("\n");
	}
}

// An upload that runs off the end of micro memory.
static void testOverrun(VIF::Unit *unit, const char *name) {
	const u32 instructions = microSize(unit) / 8;

	printf("%s MPG past the end:\n", name);
	unit->regs->fbrst = VIF::FBRST_RST;
	clearMicro(unit);
	fillPattern(8, 0x3000);
	upload(unit, (u16)(instructions - 4), 8, 8);

	printf("  last four:");
	for (u32 i = instructions - 4; i < instructions; ++i) {
		printf(" ");
		printInstruction(unit, i);
	}
	printf("\n  first four:");
	for (u32 i = 0; i < 4; ++i) {
		printf(" ");
		printInstruction(unit, i);
	}
	printf("\n");
}

static void testStatus(VIF::Unit *unit, const char *name) {
	unit->regs->fbrst = VIF::FBRST_RST;
	clearMicro(unit);
	fillPattern(4, 0x4000);
	upload(unit, 0, 4, 4);

	printf("%s after MPG: stat %08x num %08x code %08x\n", name,
	       unit->regs->stat.bits_, unit->regs->num, unit->regs->code);
}

static void doTest(VIF::Unit *unit, const char *name) {
	testAddress(unit, name);
	testSize(unit, name);
	testOverrun(unit, name);
	testStatus(unit, name);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	doTest(&VIF::Unit0, "VIF0");
	doTest(&VIF::Unit1, "VIF1");

	printf("-- TEST END\n");
	return 0;
}
