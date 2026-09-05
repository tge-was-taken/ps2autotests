#include <common-ee.h>
#include <ee_regs.h>
#include <kernel.h>
#include "gsregs.h"

// The display side of the graphics unit, which is all privileged registers and
// so needs nothing drawn to be read.  What the kernel left here is what a
// program inherits, and the write behaviour is what a program that sets its own
// mode depends on.

struct Named {
	const char *name;
	u32 address;
	bool readable;
};

// The registers a program writes to pick a mode.  Only some of the block reads
// back, so which ones do is part of what is recorded.
static const Named registers[] = {
	{"PMODE   ", 0x12000000, true},
	{"SMODE1  ", 0x12000010, true},
	{"SMODE2  ", 0x12000020, true},
	{"SRFSH   ", 0x12000030, true},
	{"SYNCH1  ", 0x12000040, true},
	{"SYNCH2  ", 0x12000050, true},
	{"SYNCV   ", 0x12000060, true},
	{"DISPFB1 ", 0x12000070, true},
	{"DISPLAY1", 0x12000080, true},
	{"DISPFB2 ", 0x12000090, true},
	{"DISPLAY2", 0x120000A0, true},
	{"EXTBUF  ", 0x120000B0, true},
	{"EXTDATA ", 0x120000C0, true},
	{"EXTWRITE", 0x120000D0, true},
	{"BGCOLOR ", 0x120000E0, true},
	{"CSR     ", 0x12001000, true},
	{"IMR     ", 0x12001010, true},
	{"BUSDIR  ", 0x12001040, true},
	{"SIGLBLID", 0x12001080, true},
};
static const int registerCount = sizeof(registers) / sizeof(registers[0]);

static u64 read(u32 address) {
	return *(volatile u64 *)address;
}

static void write(u32 address, u64 value) {
	*(volatile u64 *)address = value;
}

static void testWhatTheKernelLeft() {
	printf("Every privileged register as the program found it:\n");
	for (int i = 0; i < registerCount; ++i) {
		printf("  %s at %08x: %016llx\n", registers[i].name, registers[i].address,
		       read(registers[i].address));
	}
}

// The mode registers taken apart, since a program that reads them back is
// choosing its own resolution from what the machine was set to.
static void testModeFields() {
	const u64 mode1 = read(0x12000010);
	const u64 mode2 = read(0x12000020);
	const u64 sync = read(0x12000060);

	printf("The mode:\n");
	printf("  SMODE1 rc %d lc %d t1248 %d slck %d cmod %d ex %d prst %d sint %d\n",
	       (u32)(mode1 & 7), (u32)((mode1 >> 3) & 0x7F), (u32)((mode1 >> 10) & 3),
	       (u32)((mode1 >> 12) & 1), (u32)((mode1 >> 13) & 3),
	       (u32)((mode1 >> 15) & 1), (u32)((mode1 >> 16) & 1),
	       (u32)((mode1 >> 17) & 1));
	printf("  SMODE2 int %d ffmd %d dpms %d\n", (u32)(mode2 & 1),
	       (u32)((mode2 >> 1) & 1), (u32)((mode2 >> 2) & 3));
	printf("  SYNCV vfp %d vfpe %d vbp %d vbpe %d vdp %d vs %d\n",
	       (u32)(sync & 0x3FF), (u32)((sync >> 10) & 0x3FF),
	       (u32)((sync >> 20) & 0xFFF), (u32)((sync >> 32) & 0x3FF),
	       (u32)((sync >> 42) & 0x7FF), (u32)((sync >> 53) & 0x7FF));
}

// The two display rectangles, which say where each circuit puts its picture.
static void testDisplayFields() {
	static const u32 addresses[] = {0x12000080, 0x120000A0};
	static const char *const names[] = {"DISPLAY1", "DISPLAY2"};

	printf("The display rectangles:\n");
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		const u64 value = read(addresses[i]);
		printf("  %s dx %4d dy %4d magh %d magv %d dw %4d dh %4d\n", names[i],
		       (u32)(value & 0xFFF), (u32)((value >> 12) & 0x7FF),
		       (u32)((value >> 23) & 0xF), (u32)((value >> 27) & 3),
		       (u32)((value >> 32) & 0xFFF), (u32)((value >> 44) & 0x7FF));
	}

	static const u32 buffers[] = {0x12000070, 0x12000090};
	static const char *const bufferNames[] = {"DISPFB1", "DISPFB2"};
	for (unsigned i = 0; i < sizeof(buffers) / sizeof(buffers[0]); ++i) {
		const u64 value = read(buffers[i]);
		printf("  %s base %5d width %3d format %02x x %4d y %4d\n", bufferNames[i],
		       (u32)(value & 0x1FF), (u32)((value >> 9) & 0x3F),
		       (u32)((value >> 15) & 0x1F), (u32)((value >> 32) & 0x7FF),
		       (u32)((value >> 43) & 0x7FF));
	}
}

// Which bits each register keeps, taken by writing all ones and reading back.
// The mode registers are put back afterwards, since the picture depends on
// them.
static void testWhichBitsStick() {
	printf("Writing ones to each register and reading back:\n");
	for (int i = 0; i < registerCount; ++i) {
		// The status register clears bits rather than holding them, and the
		// mask register hides interrupts, so both are left to their own tests.
		if (registers[i].address == 0x12001000 || registers[i].address == 0x12001010) {
			continue;
		}
		const u64 saved = read(registers[i].address);
		write(registers[i].address, 0xFFFFFFFFFFFFFFFFULL);
		const u64 ones = read(registers[i].address);
		write(registers[i].address, 0);
		const u64 zeros = read(registers[i].address);
		write(registers[i].address, saved);
		printf("  %s ones %016llx zeros %016llx, back %s\n", registers[i].name,
		       ones, zeros, read(registers[i].address) == saved ? "yes" : "no");
	}
}

// The background colour, which is the one display register with no effect on
// timing and so is safe to write outright.
static void testBackground() {
	static const u64 values[] = {0x000000, 0xFFFFFF, 0x0000FF, 0x00FF00, 0xFF0000,
	                             0xFFFFFFFFFFFFFFFFULL};

	printf("The background colour:\n");
	const u64 saved = read(0x120000E0);
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		write(0x120000E0, values[i]);
		printf("  wrote %016llx, reads %016llx\n", values[i], read(0x120000E0));
	}
	write(0x120000E0, saved);
}

// The blend the two circuits are combined with, which is the only part of
// PMODE a program changes at run time.
static void testCircuitBlend() {
	printf("PMODE:\n");
	const u64 saved = read(0x12000000);
	printf("  as found: %016llx, en1 %d en2 %d mmod %d amod %d slbg %d alp %02x\n",
	       saved, (u32)(saved & 1), (u32)((saved >> 1) & 1),
	       (u32)((saved >> 5) & 1), (u32)((saved >> 6) & 1),
	       (u32)((saved >> 7) & 1), (u32)((saved >> 8) & 0xFF));

	static const u64 values[] = {0x0000000000000001ULL, 0x0000000000000002ULL,
	                             0x0000000000000003ULL, 0x000000000000FF00ULL};
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		write(0x12000000, values[i]);
		printf("  wrote %016llx, reads %016llx\n", values[i], read(0x12000000));
	}
	write(0x12000000, saved);
}

// The register that says which way the bus runs, which a transfer out of the
// unit has to set.
static void testBusDirection() {
	printf("BUSDIR:\n");
	const u64 saved = read(0x12001040);
	for (u64 value = 0; value < 3; ++value) {
		write(0x12001040, value);
		printf("  wrote %d, reads %016llx\n", (u32)value, read(0x12001040));
	}
	write(0x12001040, 0xFFFFFFFFFFFFFFFFULL);
	printf("  wrote ones, reads %016llx\n", read(0x12001040));
	write(0x12001040, saved);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testWhatTheKernelLeft();
	testModeFields();
	testDisplayFields();
	testCircuitBlend();
	testBackground();
	testBusDirection();
	testWhichBitsStick();

	printf("-- TEST END\n");
	return 0;
}
