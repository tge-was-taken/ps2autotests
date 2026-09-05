#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>
#include "spu2regs.h"

// Getting data into the sound memory and back out again.  The transfer runs on
// the input processor's own controller rather than through a driver, so what is
// recorded is the register state around it rather than any library's answer.

#define IOP_DMA4_MADR 0x1F8010C0
#define IOP_DMA4_BCR 0x1F8010C4
#define IOP_DMA4_CHCR 0x1F8010C8
#define IOP_DMA_PCR 0x1F8010F0
#define IOP_DMA_ICR 0x1F8010F4

static u32 sample[256] __attribute__((aligned(64)));
static u32 back[256] __attribute__((aligned(64)));

static u32 read32(u32 address) {
	return *(volatile u32 *)address;
}

static void write32(u32 address, u32 value) {
	*(volatile u32 *)address = value;
}

// Bit fifteen of the attribute register turns the core on.  Turning it off
// and on again is the closest thing to a reset a program can ask for.
static const u16 coreEnable = 0x8000;

static void restartCore(void) {
	coreWrite(SPU2_CORE0, SPU2_ATTR, 0);
	DelayThread(10000);
	coreWrite(SPU2_CORE0, SPU2_ATTR, coreEnable);
	DelayThread(10000);
}

static void setTransferAddress(u32 address) {
	coreWrite(SPU2_CORE0, SPU2_TSAH, (u16)(address >> 16));
	coreWrite(SPU2_CORE0, SPU2_TSAL, (u16)address);
}

static void printState(const char *what) {
	printf("  %-26s attr %04x stat %04x tsa %04x %04x chcr %08x madr %08x\n",
	       what, coreRead(SPU2_CORE0, SPU2_ATTR), coreRead(SPU2_CORE0, SPU2_STATX),
	       coreRead(SPU2_CORE0, SPU2_TSAH), coreRead(SPU2_CORE0, SPU2_TSAL),
	       read32(IOP_DMA4_CHCR), read32(IOP_DMA4_MADR));
}

// The controller registers on their own, with nothing running.
static void testChannelRegisters(void) {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x001FFFFF, 0x0000000F};
	unsigned i;

	printf("The channel registers:\n");
	printf("  PCR %08x ICR %08x\n", read32(IOP_DMA_PCR), read32(IOP_DMA_ICR));

	for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		write32(IOP_DMA4_MADR, values[i]);
		write32(IOP_DMA4_BCR, values[i]);
		printf("  wrote %08x: madr %08x bcr %08x\n", values[i],
		       read32(IOP_DMA4_MADR), read32(IOP_DMA4_BCR));
	}
	write32(IOP_DMA4_MADR, 0);
	write32(IOP_DMA4_BCR, 0);
}

// A block of memory sent into the sound memory, and the state either side of
// the transfer.
static int sendToSound(u32 soundAddress, u32 words) {
	int spins;

	write32(IOP_DMA_PCR, read32(IOP_DMA_PCR) | 0x000F0000);

	coreWrite(SPU2_CORE0, SPU2_ATTR, coreEnable | 0x0020);
	setTransferAddress(soundAddress);

	write32(IOP_DMA4_MADR, (u32)sample & 0x00FFFFFF);
	write32(IOP_DMA4_BCR, (words / 16) << 16 | 16);
	write32(IOP_DMA4_CHCR, 0x01000201);

	for (spins = 0; spins < 200000; ++spins) {
		if ((read32(IOP_DMA4_CHCR) & 0x01000000) == 0) {
			return spins;
		}
	}
	write32(IOP_DMA4_CHCR, 0);
	return -1;
}

// The same block read back out, which needs the core told which way the data
// is going.
static int readFromSound(u32 soundAddress, u32 words) {
	int spins;

	memset(back, 0, sizeof(back));

	coreWrite(SPU2_CORE0, SPU2_ATTR, coreEnable | 0x0040);
	setTransferAddress(soundAddress);

	write32(IOP_DMA4_MADR, (u32)back & 0x00FFFFFF);
	write32(IOP_DMA4_BCR, (words / 16) << 16 | 16);
	write32(IOP_DMA4_CHCR, 0x01000200);

	for (spins = 0; spins < 200000; ++spins) {
		if ((read32(IOP_DMA4_CHCR) & 0x01000000) == 0) {
			return spins;
		}
	}
	write32(IOP_DMA4_CHCR, 0);
	return -1;
}

static void testRoundTrip(void) {
	unsigned i;
	int sent;
	int received;
	int same = 0;

	for (i = 0; i < 256; ++i) {
		sample[i] = 0x5A5A0000 + i;
	}

	printf("A block through the sound memory and back:\n");
	restartCore();
	printState("after a reset");

	sent = sendToSound(0x5000, 256);
	printf("  send finished after %d checks\n", sent);
	printState("after sending");

	received = readFromSound(0x5000, 256);
	printf("  read finished after %d checks\n", received);
	printState("after reading");

	for (i = 0; i < 256; ++i) {
		if (back[i] == sample[i]) {
			same++;
		}
	}
	printf("  %d of 256 words came back the same\n", same);
	printf("  first four out %08x %08x %08x %08x\n", back[0], back[1], back[2],
	       back[3]);

	coreWrite(SPU2_CORE0, SPU2_ATTR, 0);
	write32(IOP_DMA4_CHCR, 0);
}

// Each address the transfer can start at, which says how much memory the core
// has and where it folds.
static void testAddresses(void) {
	static const u32 addresses[] = {0x1000, 0x5000, 0x40000, 0x80000, 0xFFFF0,
	                                0x100000, 0x1FFFF0};
	unsigned i;

	for (i = 0; i < 64; ++i) {
		sample[i] = 0xA1A10000 + i;
	}

	printf("A short block at each address:\n");
	for (i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		int sent;
		int received;
		int same = 0;
		unsigned j;

		restartCore();
		sent = sendToSound(addresses[i], 64);
		received = readFromSound(addresses[i], 64);
		for (j = 0; j < 64; ++j) {
			if (back[j] == sample[j]) {
				same++;
			}
		}
		printf("  %08x: send %d read %d, %d of 64 the same, first %08x\n",
		       addresses[i], sent, received, same, back[0]);
	}

	coreWrite(SPU2_CORE0, SPU2_ATTR, 0);
	write32(IOP_DMA4_CHCR, 0);
}

// The mode field in the attribute register, which is what picks the direction.
static void testAttributeModes(void) {
	static const u16 modes[] = {0x0000, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050};
	unsigned i;

	printf("The transfer mode in the attribute register:\n");
	for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
		coreWrite(SPU2_CORE0, SPU2_ATTR, modes[i]);
		printf("  wrote %04x, reads %04x, status %04x\n", modes[i],
		       coreRead(SPU2_CORE0, SPU2_ATTR), coreRead(SPU2_CORE0, SPU2_STATX));
	}
	coreWrite(SPU2_CORE0, SPU2_ATTR, 0);
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testChannelRegisters();
	testAttributeModes();
	testRoundTrip();
	testAddresses();

	printf("-- TEST END\n");
	return 1;
}
