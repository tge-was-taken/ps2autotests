#include <common-ee.h>
#include <kernel.h>
#include <string.h>
#include "../dmaregs.h"
#include "../dmasend.h"
#include "emit_vifcode.h"
#include "vifunits.h"

// Every command byte, including the ones no unit implements.  A packet that
// ends in a marker says whether the unit carried on past the code or stopped
// on it, and the error register says which of the two errors it raised.

static const u16 markValue = 0x5A5A;

struct Outcome {
	u32 stat;
	u32 err;
	u32 mark;
	u32 code;
	u32 num;
};

static void reset(VIF::Unit *unit) {
	unit->regs->fbrst = VIF::FBRST_RST;
	unit->regs->err = (VIF::RegERRBits)0;
	unit->regs->mrk = 0;
}

static void collect(VIF::Unit *unit, Outcome &outcome) {
	outcome.stat = unit->regs->stat.bits_;
	outcome.err = unit->regs->err.bits_;
	outcome.mark = unit->regs->mrk;
	outcome.code = unit->regs->code;
	outcome.num = unit->regs->num;
}

static void printOutcome(const char *name, const Outcome &outcome) {
	printf("  %-14s stat %08x err %08x mark %04x code %08x num %02x %s\n", name,
	       outcome.stat, outcome.err, outcome.mark & 0xFFFF, outcome.code,
	       outcome.num & 0xFF, outcome.mark == markValue ? "reached" : "stopped");
}

// A raw command word followed by a mark, so a code the unit rejects shows up
// as the mark never landing.
static void sendRaw(VIF::Unit *unit, u32 code, Outcome &outcome) {
	reset(unit);

	VIF::Packet packet(64);
	packet.Data32(code);
	packet.MARK(markValue);
	packet.NOP();
	packet.NOP();

	DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);
	collect(unit, outcome);
}

static void testEveryCommand(VIF::Unit *unit, const char *name) {
	printf("%s every command byte:\n", name);
	for (u32 command = 0; command < 0x100; ++command) {
		// The unpack range carries data, so a bare code there would eat the
		// mark and say nothing about the code itself.
		if (command >= 0x60) {
			continue;
		}
		Outcome outcome;
		sendRaw(unit, command << 24, outcome);

		char label[24];
		sprintf(label, "%02x", command);
		printOutcome(label, outcome);
	}
}

// The interrupt bit on a code stops the unit until the stall is cleared.
static void testInterruptBit(VIF::Unit *unit, const char *name) {
	static const u32 commands[] = {0x00, 0x01, 0x05, 0x07, 0x14, 0x20};

	printf("%s with the interrupt bit set:\n", name);
	for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
		Outcome outcome;
		sendRaw(unit, (commands[i] | 0x80) << 24, outcome);

		char label[24];
		sprintf(label, "%02x with int", commands[i]);
		printOutcome(label, outcome);
		unit->regs->fbrst = VIF::FBRST_STC;
	}
}

// The two error mask bits decide whether a bad code stalls or is ignored.
static void testErrorMask(VIF::Unit *unit, const char *name) {
	static const u32 masks[] = {0, VIF::ERR_MII, VIF::ERR_ME0, VIF::ERR_ME1,
	                            VIF::ERR_ME0 | VIF::ERR_ME1};
	static const char *const maskNames[] = {"none", "MII", "ME0", "ME1", "ME0+ME1"};

	printf("%s a reserved code against each error mask:\n", name);
	for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
		reset(unit);
		unit->regs->err = (VIF::RegERRBits)masks[i];

		VIF::Packet packet(64);
		packet.Data32(0x0F000000);
		packet.MARK(markValue);
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);

		Outcome outcome;
		collect(unit, outcome);
		printOutcome(maskNames[i], outcome);
	}
	unit->regs->err = (VIF::RegERRBits)0;
}

// The immediate field of the codes that keep one.
static void testImmediates(VIF::Unit *unit, const char *name) {
	static const u16 values[] = {0x0000, 0x0001, 0x00FF, 0x03FF, 0x7FFF, 0xFFFF};

	printf("%s MARK and ITOP immediates:\n", name);
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		reset(unit);

		VIF::Packet packet(64);
		packet.MARK(values[i]);
		packet.ITOP(values[i]);
		packet.NOP();
		packet.NOP();
		DMA::SendSimple(unit->dmaChannel, packet.Raw(), 16);

		printf("  %04x: mark %04x itops %04x\n", values[i], unit->regs->mrk & 0xFFFF,
		       unit->regs->itops & 0xFFFF);
	}
}

static void doTest(VIF::Unit *unit, const char *name) {
	testEveryCommand(unit, name);
	testInterruptBit(unit, name);
	testErrorMask(unit, name);
	testImmediates(unit, name);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	doTest(&VIF::Unit0, "VIF0");
	doTest(&VIF::Unit1, "VIF1");

	printf("-- TEST END\n");
	return 0;
}
