#include <common-ee.h>
#include <ee_regs.h>
#include <kernel.h>
#include <string.h>
#include "../dmaregs.h"
#include "../dmasend.h"
#include "emit_vifcode.h"
#include "vifunits.h"

// The unit can be fed by writing its fifo directly instead of by dma, and the
// fifo count in the status register says how much it is holding.

static void reset(VIF::Unit *unit) {
	unit->regs->fbrst = VIF::FBRST_RST;
}

static u32 fifoCount(VIF::Unit *unit) {
	return (unit->regs->stat.bits_ >> VIF::STAT_FQC_SHIFT) & 0x1F;
}

// A packet written a quadword at a time by the processor rather than by the
// controller.
static void writeFifo(VIF::Unit *unit, const u32 *words, int quadwords) {
	for (int q = 0; q < quadwords; ++q) {
		u32 __attribute__((aligned(16))) quad[4];
		memcpy(quad, words + q * 4, sizeof(quad));
		*unit->fifo = *(vu128 *)quad;
	}
}

static void testDirectWrite(VIF::Unit *unit, const char *name) {
	printf("%s writing the fifo:\n", name);

	reset(unit);
	printf("  after reset: stat %08x count %u\n", unit->regs->stat.bits_,
	       fifoCount(unit));

	u32 __attribute__((aligned(16))) codes[4] = {0, 0, 0, 0};
	// MARK 0x1234, then three nops, as one quadword of vifcodes.
	codes[0] = (0x07u << 24) | 0x1234;
	writeFifo(unit, codes, 1);

	printf("  after a MARK quadword: stat %08x count %u mark %04x\n",
	       unit->regs->stat.bits_, fifoCount(unit), unit->regs->mrk & 0xFFFF);
}

// An unpack sent the same way, so the data path and the code path are both
// exercised without the controller.
static void testUnpackThroughFifo(VIF::Unit *unit, const char *name) {
	printf("%s unpacking through the fifo:\n", name);

	reset(unit);
	memset(unit->vuMem, 0xFF, 32);
	SyncDCache(unit->vuMem, unit->vuMem + 32);

	u32 __attribute__((aligned(16))) quad[8];
	quad[0] = 0x01000101;                    // STCYCL 1, 1
	quad[1] = (0x6Cu << 24) | (1 << 16) | 0; // UNPACK V4-32, one element, addr 0
	quad[2] = 0xA1A1A1A1;
	quad[3] = 0xB2B2B2B2;
	quad[4] = 0xC3C3C3C3;
	quad[5] = 0xD4D4D4D4;
	quad[6] = 0;
	quad[7] = 0;
	writeFifo(unit, quad, 2);

	SyncDCache(unit->vuMem, unit->vuMem + 32);
	const volatile u32 *words = (const volatile u32 *)unit->vuMem;
	printf("  vu memory: %08x %08x %08x %08x, count %u, num %02x\n", words[0],
	       words[1], words[2], words[3], fifoCount(unit), unit->regs->num & 0xFF);
}

// How the count moves as quadwords go in without the unit being able to retire
// them, which is what a stall looks like from outside.
static void testCountWhileStalled(VIF::Unit *unit, const char *name) {
	printf("%s count while stalled:\n", name);

	reset(unit);
	unit->regs->fbrst = VIF::FBRST_STP;

	u32 __attribute__((aligned(16))) nops[4] = {0, 0, 0, 0};
	for (int i = 0; i < 6; ++i) {
		writeFifo(unit, nops, 1);
		printf("  after %d quadwords: stat %08x count %u\n", i + 1,
		       unit->regs->stat.bits_, fifoCount(unit));
	}

	unit->regs->fbrst = VIF::FBRST_STC;
	printf("  after clearing the stall: stat %08x count %u\n",
	       unit->regs->stat.bits_, fifoCount(unit));
	reset(unit);
}

// The direction bit, which turns vif1 around to read from the graphics unit.
static void testDirection() {
	printf("VIF1 direction bit:\n");
	VIF::Unit *unit = &VIF::Unit1;

	reset(unit);
	printf("  after reset: stat %08x\n", unit->regs->stat.bits_);

	VIF::Packet packet(64);
	packet.MARK(0x7777);
	packet.NOP();
	packet.NOP();
	packet.NOP();
	DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
	printf("  after a packet: stat %08x, FDR %d\n", unit->regs->stat.bits_,
	       (unit->regs->stat.bits_ & VIF::STAT_FDR) ? 1 : 0);
}

static void doTest(VIF::Unit *unit, const char *name) {
	testDirectWrite(unit, name);
	testUnpackThroughFifo(unit, name);
	testCountWhileStalled(unit, name);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	doTest(&VIF::Unit0, "VIF0");
	doTest(&VIF::Unit1, "VIF1");
	testDirection();

	printf("-- TEST END\n");
	return 0;
}
