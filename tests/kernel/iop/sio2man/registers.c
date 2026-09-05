#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>

// The serial block the controller and memory card ports hang off.  Nothing
// here writes to a card: the only command sent is the one that asks a pad for
// its identity, which a machine with nothing plugged in answers just as well.

#define SIO2_SEND3 0x1F808200
#define SIO2_SEND1 0x1F808240
#define SIO2_FIFOIN 0x1F808260
#define SIO2_FIFOOUT 0x1F808264
#define SIO2_CTRL 0x1F808268
#define SIO2_RECV1 0x1F80826C
#define SIO2_RECV2 0x1F808270
#define SIO2_RECV3 0x1F808274
#define SIO2_ISTAT 0x1F808280

static u32 read32(u32 address) {
	return *(volatile u32 *)address;
}

static void write32(u32 address, u32 value) {
	*(volatile u32 *)address = value;
}

static u8 read8(u32 address) {
	return *(volatile u8 *)address;
}

static void write8(u32 address, u8 value) {
	*(volatile u8 *)address = value;
}

static void dump(const char *what) {
	u32 address;

	printf("%s:\n", what);
	for (address = 0x1F808200; address < 0x1F808290; address += 16) {
		printf("  %08x: %08x %08x %08x %08x\n", address, read32(address),
		       read32(address + 4), read32(address + 8), read32(address + 12));
	}
	printf("  recv1 %08x recv2 %08x recv3 %08x ctrl %08x istat %08x\n",
	       read32(SIO2_RECV1), read32(SIO2_RECV2), read32(SIO2_RECV3),
	       read32(SIO2_CTRL), read32(SIO2_ISTAT));
}

// Which bits the queue registers keep.
static void testQueueRegisters(void) {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x0001FFFF, 0x000001FF};
	unsigned i;
	int entry;

	printf("The send queue:\n");
	for (entry = 0; entry < 4; ++entry) {
		for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
			write32(SIO2_SEND3 + entry * 4, values[i]);
			printf("  send3[%d] wrote %08x, reads %08x\n", entry, values[i],
			       read32(SIO2_SEND3 + entry * 4));
		}
	}
	for (entry = 0; entry < 4; ++entry) {
		write32(SIO2_SEND1 + entry * 4, 0xFFFFFFFF);
		printf("  send1/2[%d] wrote ffffffff, reads %08x\n", entry,
		       read32(SIO2_SEND1 + entry * 4));
	}
	for (entry = 0; entry < 16; ++entry) {
		write32(SIO2_SEND3 + entry * 4, 0);
	}
	for (entry = 0; entry < 8; ++entry) {
		write32(SIO2_SEND1 + entry * 4, 0);
	}
}

// The status register, which is written back to clear rather than to set.
static void testStatus(void) {
	printf("The interrupt status:\n");
	printf("  reads %08x\n", read32(SIO2_ISTAT));
	write32(SIO2_ISTAT, 0);
	printf("  after writing zero %08x\n", read32(SIO2_ISTAT));
	write32(SIO2_ISTAT, 0xFFFFFFFF);
	printf("  after writing ones %08x\n", read32(SIO2_ISTAT));
}

// The command that asks a pad for its identity, which is four bytes out and
// nine back.  A port with nothing in it answers with the failure code rather
// than not answering.
static void testPadIdentity(void) {
	static const u8 command[] = {0x01, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	                             0x00};
	int port;

	printf("Asking each port for a pad:\n");
	for (port = 0; port < 2; ++port) {
		int i;
		int spins;

		write32(SIO2_ISTAT, 0xFFFFFFFF);
		write32(SIO2_CTRL, 0x0C);

		write32(SIO2_SEND3, (port << 1) | (9 << 18) | (9 << 8) | 1);
		write32(SIO2_SEND3 + 4, 0);
		write32(SIO2_SEND1 + port * 8, 0x00030064);
		write32(SIO2_SEND1 + port * 8 + 4, 0x0000FFFF);

		for (i = 0; i < 9; ++i) {
			write8(SIO2_FIFOIN, command[i]);
		}

		write32(SIO2_CTRL, 0x0F);

		for (spins = 0; spins < 200000; ++spins) {
			if ((read32(SIO2_ISTAT) & 1) != 0) {
				break;
			}
		}

		printf("  port %d: waited %d, istat %08x recv1 %08x recv3 %08x\n", port,
		       spins, read32(SIO2_ISTAT), read32(SIO2_RECV1), read32(SIO2_RECV3));
		printf("    answer");
		for (i = 0; i < 9; ++i) {
			printf(" %02x", read8(SIO2_FIFOOUT));
		}
		printf("\n");

		write32(SIO2_ISTAT, 0xFFFFFFFF);
		write32(SIO2_CTRL, 0x0C);
	}
}

// The control register, whose low bits start a queue and reset the block.
static void testControl(void) {
	static const u32 values[] = {0x00, 0x01, 0x0C, 0x0F, 0xFFFFFFFF};
	unsigned i;

	printf("The control register:\n");
	for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		write32(SIO2_CTRL, values[i]);
		printf("  wrote %08x, reads %08x, istat %08x\n", values[i],
		       read32(SIO2_CTRL), read32(SIO2_ISTAT));
	}
	write32(SIO2_CTRL, 0x0C);
	write32(SIO2_ISTAT, 0xFFFFFFFF);
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	dump("As the program found it");
	testStatus();
	testQueueRegisters();
	testControl();
	testPadIdentity();
	dump("After the commands");

	printf("-- TEST END\n");
	return 1;
}
