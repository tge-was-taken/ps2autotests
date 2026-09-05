#include <common-ee.h>
#include <string.h>
#include "ipuregs.h"

// The command register and the state around it.  A command that waits for data
// it will never get leaves the unit busy, so every case resets afterwards and
// the record says whether the unit came back on its own.

static void printState(const char *what) {
	const u32 control = *IPU::CTRL;
	printf("  %-26s ctrl %08x bp %08x top %08x  in %d out %d busy %d\n", what,
	       control, *IPU::BP, *IPU::TOP, control & 0xF, (control >> 4) & 0xF,
	       (control >> 31) & 1);
}

static void testReset() {
	printf("Reset:\n");

	printState("as the program found it");
	IPU::reset();
	printState("after a reset");

	// The reset bit is documented as clearing itself, so a read straight after
	// says whether it is a pulse or a level.
	*IPU::CTRL = IPU::CTRL_RST;
	printf("  the reset bit reads back %d\n", (*IPU::CTRL >> 30) & 1);
	IPU::reset();
}

// The bits the control register keeps when a program writes them directly.
static void testControlWrites() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x3FFFFFFF, 0x0000FFFF,
	                             0x00FF0000, 0x40000000};

	printf("Writing the control register:\n");
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		IPU::reset();
		*IPU::CTRL = values[i];
		printf("  wrote %08x, reads %08x\n", values[i], *IPU::CTRL);
	}
	IPU::reset();
}

// The command that sets the bit position, which is the only one that needs no
// data behind it.
static void testClearCommand() {
	static const u32 positions[] = {0, 1, 7, 8, 31, 32, 63, 64, 127, 128, 0xFFFF};

	printf("The clear command with each bit position:\n");
	for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
		IPU::reset();
		IPU::write((IPU::CMD_BCLR << 28) | positions[i]);
		const bool idle = IPU::waitForIdle();

		char name[40];
		sprintf(name, "position %5u, idle %s", positions[i], idle ? "yes" : "no");
		printState(name);
	}
	IPU::reset();
}

// The threshold command, whose two fields land in the control register.
static void testThresholdCommand() {
	static const u32 values[] = {0x00000000, 0x000001FF, 0x01FF0000, 0x01FF01FF,
	                             0x0FFFFFFF};

	printf("The threshold command:\n");
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		IPU::reset();
		IPU::write((IPU::CMD_SETTH << 28) | values[i]);
		const bool idle = IPU::waitForIdle();

		char name[40];
		sprintf(name, "value %08x, idle %s", values[i], idle ? "yes" : "no");
		printState(name);
	}
	IPU::reset();
}

// Every command code with nothing in the input, so the record says which ones
// finish on their own and which wait.
static void testEveryCode() {
	printf("Each command code on an empty input:\n");
	for (u32 code = 0; code < 16; ++code) {
		// Both a command and a reset written while the unit is busy block the
		// bus, so a code that leaves it busy ends the sweep rather than being
		// recovered from.
		if (IPU::isBusy()) {
			printf("  code %2u: not sent, the unit is still busy\n", code);
			return;
		}
		if (!IPU::reset()) {
			printf("  code %2u: not sent, the reset did not take\n", code);
			return;
		}
		IPU::write(code << 28);
		const bool idle = IPU::waitForIdle();

		char name[40];
		sprintf(name, "code %2u, idle %s", code, idle ? "yes" : "no ");
		printState(name);
	}
	IPU::reset();
}

// What the command register reads back, which is where a decode leaves its
// result.
static void testCommandReadback() {
	printf("Reading the command register:\n");

	IPU::reset();
	printf("  after a reset: %016llx\n", *IPU::CMD);

	IPU::write((IPU::CMD_BCLR << 28) | 16);
	IPU::waitForIdle();
	printf("  after a clear to 16: %016llx\n", *IPU::CMD);

	IPU::write((IPU::CMD_SETTH << 28) | 0x00100010);
	IPU::waitForIdle();
	printf("  after a threshold: %016llx\n", *IPU::CMD);

	IPU::reset();
}

// The counters, which say how much the two ends hold.
static void testFifoCounters() {
	printf("The input counter as quadwords go in:\n");

	IPU::reset();
	printState("empty");

	u32 quad[4] = {0x00000001, 0x00000002, 0x00000003, 0x00000004};
	for (int i = 0; i < 10; ++i) {
		quad[0] = 0x10000000 + i;
		*IPU::IN_FIFO = *(volatile u128 *)quad;

		char name[32];
		sprintf(name, "after %2d quadwords", i + 1);
		printState(name);
	}

	IPU::reset();
	printState("after a reset");
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testReset();
	testControlWrites();
	testClearCommand();
	testThresholdCommand();
	testCommandReadback();
	testFifoCounters();
	testEveryCode();

	printf("-- TEST END\n");
	return 0;
}
