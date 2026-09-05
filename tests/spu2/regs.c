#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>
#include "spu2regs.h"

// The whole register block, read as it was found, after a reset, and with ones
// written into it.  The addresses are the record: a register that holds a bit
// the hardware does not is a difference a sound driver runs into straight away.

static void dump(const char *what) {
	u32 address;

	printf("%s:\n", what);
	for (address = SPU2_BASE; address < SPU2_END; address += 16) {
		printf("  %08x: %04x %04x %04x %04x %04x %04x %04x %04x\n", address,
		       spu2Read(address), spu2Read(address + 2), spu2Read(address + 4),
		       spu2Read(address + 6), spu2Read(address + 8),
		       spu2Read(address + 10), spu2Read(address + 12),
		       spu2Read(address + 14));
	}
}

// Bit fifteen of the attribute register turns a core on.  Turning both off and
// on again is the closest thing to a reset a program can ask for.
static const u16 coreEnable = 0x8000;

static void restartCores(void) {
	coreWrite(SPU2_CORE0, SPU2_ATTR, 0);
	coreWrite(SPU2_CORE1, SPU2_ATTR, 0);
	DelayThread(10000);
	coreWrite(SPU2_CORE0, SPU2_ATTR, coreEnable);
	coreWrite(SPU2_CORE1, SPU2_ATTR, coreEnable);
	DelayThread(10000);
}

// Which bits each register holds, taken by writing ones and then zeros.  The
// two are printed together so a register that reads back neither is obvious.
static void testWhichBitsStick(void) {
	u32 address;

	printf("Ones and zeros written to each register:\n");
	for (address = SPU2_BASE; address < SPU2_END; address += 2) {
		u16 ones;
		u16 zeros;

		spu2Write(address, 0xFFFF);
		ones = spu2Read(address);
		spu2Write(address, 0x0000);
		zeros = spu2Read(address);

		if (ones == 0 && zeros == 0) {
			continue;
		}
		printf("  %08x: ones %04x zeros %04x\n", address, ones, zeros);
	}
}

// The status register of each core, read a few times so a bit that moves on
// its own is not mistaken for one this program set.
static void testStatus(void) {
	int i;

	printf("The status register of each core:\n");
	for (i = 0; i < 6; ++i) {
		printf("  core 0 %04x, core 1 %04x\n", coreRead(SPU2_CORE0, SPU2_STATX),
		       coreRead(SPU2_CORE1, SPU2_STATX));
		DelayThread(2000);
	}
}

// The transfer address, which is a pair of registers holding one value.
static void testTransferAddress(void) {
	static const u32 addresses[] = {0, 0x1000, 0x2000, 0x100000, 0x1FFFFF,
	                                0x200000, 0xFFFFFFFF};
	unsigned i;

	printf("The transfer address:\n");
	for (i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		coreWrite(SPU2_CORE0, SPU2_TSAH, (u16)(addresses[i] >> 16));
		coreWrite(SPU2_CORE0, SPU2_TSAL, (u16)addresses[i]);
		printf("  wrote %08x, reads %04x %04x\n", addresses[i],
		       coreRead(SPU2_CORE0, SPU2_TSAH), coreRead(SPU2_CORE0, SPU2_TSAL));
	}
}

// The key on and key off registers, which are write only on some revisions and
// read back on others.
static void testKeyRegisters(void) {
	static const u32 values[] = {0x000000, 0x000001, 0xFFFFFF, 0xFF000000};
	unsigned i;

	printf("Key on and key off:\n");
	for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		coreWrite(SPU2_CORE0, SPU2_KON, (u16)values[i]);
		coreWrite(SPU2_CORE0, SPU2_KON + 2, (u16)(values[i] >> 16));
		printf("  key on %08x reads %04x %04x, ENDX %04x %04x\n", values[i],
		       coreRead(SPU2_CORE0, SPU2_KON), coreRead(SPU2_CORE0, SPU2_KON + 2),
		       coreRead(SPU2_CORE0, SPU2_ENDX),
		       coreRead(SPU2_CORE0, SPU2_ENDX + 2));

		coreWrite(SPU2_CORE0, SPU2_KOFF, (u16)values[i]);
		coreWrite(SPU2_CORE0, SPU2_KOFF + 2, (u16)(values[i] >> 16));
		printf("  key off %08x reads %04x %04x\n", values[i],
		       coreRead(SPU2_CORE0, SPU2_KOFF),
		       coreRead(SPU2_CORE0, SPU2_KOFF + 2));
	}
	coreWrite(SPU2_CORE0, SPU2_KOFF, 0xFFFF);
	coreWrite(SPU2_CORE0, SPU2_KOFF + 2, 0x00FF);
}

// The attribute register, which holds the mode the rest of the core runs in.
static void testAttribute(void) {
	static const u16 values[] = {0x0000, 0xFFFF, 0x8000, 0x4000, 0x0030, 0x0040};
	unsigned i;

	printf("The attribute register:\n");
	for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		coreWrite(SPU2_CORE0, SPU2_ATTR, values[i]);
		printf("  wrote %04x, reads %04x, status %04x\n", values[i],
		       coreRead(SPU2_CORE0, SPU2_ATTR), coreRead(SPU2_CORE0, SPU2_STATX));
	}
	coreWrite(SPU2_CORE0, SPU2_ATTR, 0);
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	dump("As the program found it");
	restartCores();
	dump("After a restart");

	testStatus();
	testTransferAddress();
	testAttribute();
	testKeyRegisters();
	testWhichBitsStick();

	restartCores();
	dump("After a second restart");

	printf("-- TEST END\n");
	return 1;
}
