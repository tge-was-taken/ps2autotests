#include <common-ee.h>
#include <kernel.h>
#include <string.h>
#include "../dmaregs.h"
#include "../dmasend.h"
#include "emit_vifcode.h"
#include "vifunits.h"

// The codes that steer the unit rather than move data: the buffer registers,
// the marker, the two flushes and the three ways to start a microprogram.

static void reset(VIF::Unit *unit) {
	unit->regs->fbrst = VIF::FBRST_RST;
}

static void printRegisters(VIF::Unit *unit, const char *what) {
	printf("  %-18s stat %08x cycle %08x mode %08x num %02x mark %04x itops %04x"
	       " itop %04x base %04x ofst %04x tops %04x top %04x\n",
	       what, unit->regs->stat.bits_, unit->regs->cycle, unit->regs->mode,
	       unit->regs->num & 0xFF, unit->regs->mrk & 0xFFFF,
	       unit->regs->itops & 0xFFFF, unit->regs->itop & 0xFFFF,
	       unit->regs->base & 0xFFFF, unit->regs->ofst & 0xFFFF,
	       unit->regs->tops & 0xFFFF, unit->regs->top & 0xFFFF);
}

// Which registers each code writes, one code at a time from a known state.
static void testEachCode(VIF::Unit *unit, const char *name) {
	printf("%s one code at a time:\n", name);

	reset(unit);
	printRegisters(unit, "after reset");

	{
		VIF::Packet packet(64);
		packet.STCYCL(3, 5);
		packet.NOP();
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
		printRegisters(unit, "STCYCL 3,5");
	}
	{
		VIF::Packet packet(64);
		packet.STMOD(2);
		packet.NOP();
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
		printRegisters(unit, "STMOD 2");
	}
	{
		VIF::Packet packet(64);
		packet.MARK(0x1234);
		packet.NOP();
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
		printRegisters(unit, "MARK 1234");
	}
	{
		VIF::Packet packet(64);
		packet.ITOP(0x0055);
		packet.NOP();
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
		printRegisters(unit, "ITOP 0055");
	}
	{
		VIF::Packet packet(64);
		packet.FLUSHE();
		packet.NOP();
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
		printRegisters(unit, "FLUSHE");
	}
}

// BASE and OFFSET are documented as vif1 only, so this says what vif0 makes
// of them.
static void testBufferRegisters(VIF::Unit *unit, const char *name) {
	static const u16 bases[] = {0x000, 0x040, 0x3FF};
	static const u16 offsets[] = {0x000, 0x100, 0x3FF};

	printf("%s BASE and OFFSET:\n", name);
	for (unsigned b = 0; b < sizeof(bases) / sizeof(bases[0]); ++b) {
		for (unsigned o = 0; o < sizeof(offsets) / sizeof(offsets[0]); ++o) {
			reset(unit);

			VIF::Packet packet(64);
			packet.BASE(bases[b]);
			packet.OFFSET(offsets[o]);
			packet.NOP();
			packet.NOP();
			DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);

			char label[32];
			sprintf(label, "base %03x ofst %03x", bases[b], offsets[o]);
			printRegisters(unit, label);
		}
	}
}

// The two flushes that wait for the vu, and the one that waits for the gif as
// well, with nothing running.
static void testFlushes(VIF::Unit *unit, const char *name) {
	printf("%s flushes with nothing running:\n", name);

	reset(unit);
	{
		VIF::Packet packet(64);
		packet.FLUSHE();
		packet.MARK(0x1111);
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
		printRegisters(unit, "FLUSHE then MARK");
	}
	if (unit == &VIF::Unit1) {
		reset(unit);
		VIF::Packet packet(64);
		packet.FLUSH();
		packet.MARK(0x2222);
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
		printRegisters(unit, "FLUSH then MARK");

		reset(unit);
		VIF::Packet other(64);
		other.FLUSHA();
		other.MARK(0x3333);
		other.NOP();
		other.NOP();
		DMA::SendSimple(unit->dmaChannel, other.Raw(), 16);
		printRegisters(unit, "FLUSHA then MARK");
	}
}

// Starting a microprogram from each of the three codes, with a program that
// only ends, so what is recorded is the register state rather than any work.
static void testStart(VIF::Unit *unit, const char *name) {
	static const u64 endProgram[2] = {0x4000030000000000ull, 0x800002FF8000033Cull};

	printf("%s starting a program:\n", name);

	reset(unit);
	{
		VIF::Packet packet(128);
		packet.MPG(0, 2);
		packet.DataPtr(endProgram, sizeof(endProgram));
		packet.MSCAL(0);
		packet.NOP();
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 32);
		printRegisters(unit, "MSCAL 0");
	}
	{
		VIF::Packet packet(64);
		packet.MSCNT(0);
		packet.NOP();
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
		printRegisters(unit, "MSCNT");
	}
	if (unit == &VIF::Unit1) {
		VIF::Packet packet(64);
		packet.MSCALF(0);
		packet.NOP();
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
		printRegisters(unit, "MSCALF 0");
	}
}

// The reset and stall controls in FBRST.
static void testReset(VIF::Unit *unit, const char *name) {
	static const u32 bits[] = {VIF::FBRST_RST, VIF::FBRST_FBK, VIF::FBRST_STP,
	                           VIF::FBRST_STC};
	static const char *const names[] = {"RST", "FBK", "STP", "STC"};

	printf("%s FBRST:\n", name);
	for (unsigned i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i) {
		reset(unit);

		VIF::Packet packet(64);
		packet.MARK(0x4444);
		packet.STCYCL(2, 3);
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);

		unit->regs->fbrst = (VIF::RegFBRSTBits)bits[i];
		printRegisters(unit, names[i]);
	}
	reset(unit);
}

static void doTest(VIF::Unit *unit, const char *name) {
	testEachCode(unit, name);
	testBufferRegisters(unit, name);
	testFlushes(unit, name);
	testStart(unit, name);
	testReset(unit, name);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	doTest(&VIF::Unit0, "VIF0");
	doTest(&VIF::Unit1, "VIF1");

	printf("-- TEST END\n");
	return 0;
}
