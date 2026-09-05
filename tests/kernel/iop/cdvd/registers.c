#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>

// The disc block's registers.  Only the commands that read are sent: nothing
// here spins the drive, changes the tray, or touches the machine's settings,
// since a test that did could not be run twice.

#define CDVD_BASE 0x1F402004

#define CDVD_N_COMMAND 0x1F402004
#define CDVD_N_READY 0x1F402005
#define CDVD_ERROR 0x1F402006
#define CDVD_BREAK 0x1F402007
#define CDVD_INTR_STAT 0x1F402008
#define CDVD_STATUS 0x1F40200A
#define CDVD_TRAY_STATE 0x1F40200B
#define CDVD_S_COMMAND 0x1F402016
#define CDVD_S_READY 0x1F402017
#define CDVD_S_DATA_OUT 0x1F402018

static u8 read8(u32 address) {
	return *(volatile u8 *)address;
}

static void write8(u32 address, u8 value) {
	*(volatile u8 *)address = value;
}

static void dump(const char *what) {
	u32 address;

	printf("%s:\n", what);
	for (address = CDVD_BASE; address < CDVD_BASE + 0x40; address += 16) {
		int i;
		printf("  %08x:", address);
		for (i = 0; i < 16; ++i) {
			printf(" %02x", read8(address + i));
		}
		printf("\n");
	}
}

// The two ready registers, sampled a few times so a bit that clears on its own
// is not read as one this program cleared.
static void testReady(void) {
	int i;

	printf("The ready registers over eight reads:\n");
	printf("  n:");
	for (i = 0; i < 8; ++i) {
		printf(" %02x", read8(CDVD_N_READY));
		DelayThread(1000);
	}
	printf("\n  s:");
	for (i = 0; i < 8; ++i) {
		printf(" %02x", read8(CDVD_S_READY));
		DelayThread(1000);
	}
	printf("\n");
}

// The status and tray registers, which say what the drive is doing.
static void testStatus(void) {
	int i;

	printf("The drive status over eight reads:\n");
	for (i = 0; i < 8; ++i) {
		printf("  status %02x tray %02x error %02x intr %02x\n",
		       read8(CDVD_STATUS), read8(CDVD_TRAY_STATE), read8(CDVD_ERROR),
		       read8(CDVD_INTR_STAT));
		DelayThread(2000);
	}
}

// A command that only reads, sent the way the block expects and given a bounded
// wait.  The clock is the safest one: it changes nothing and answers straight
// away.
static int sendReadOnlyCommand(u8 command, u8 *answer, int wanted) {
	int spins;
	int received = 0;

	for (spins = 0; spins < 100000; ++spins) {
		if ((read8(CDVD_S_READY) & 0x80) == 0) {
			break;
		}
	}
	if ((read8(CDVD_S_READY) & 0x80) != 0) {
		return -1;
	}

	write8(CDVD_S_COMMAND, command);

	for (spins = 0; spins < 200000; ++spins) {
		if ((read8(CDVD_S_READY) & 0x80) == 0) {
			break;
		}
	}
	if ((read8(CDVD_S_READY) & 0x80) != 0) {
		return -2;
	}

	while (received < wanted && (read8(CDVD_S_READY) & 0x40) == 0) {
		answer[received] = read8(CDVD_S_DATA_OUT);
		received++;
	}
	return received;
}

static void testClockCommand(void) {
	u8 answer[16];
	int received;
	int i;

	memset(answer, 0, sizeof(answer));
	printf("The clock command:\n");
	received = sendReadOnlyCommand(0x08, answer, 16);
	printf("  returned %d bytes:", received);
	for (i = 0; i < 16 && i < received; ++i) {
		printf(" %02x", answer[i]);
	}
	printf("\n");
	printf("  ready %02x status %02x\n", read8(CDVD_S_READY), read8(CDVD_STATUS));
}

// The same command twice, which says whether the answer is held or produced
// afresh.
static void testRepeatedCommand(void) {
	u8 first[16];
	u8 second[16];
	int i;

	memset(first, 0, sizeof(first));
	memset(second, 0, sizeof(second));

	printf("The clock command twice:\n");
	printf("  first %d bytes, second %d bytes\n",
	       sendReadOnlyCommand(0x08, first, 16),
	       sendReadOnlyCommand(0x08, second, 16));
	printf("  first :");
	for (i = 0; i < 8; ++i) {
		printf(" %02x", first[i]);
	}
	printf("\n  second:");
	for (i = 0; i < 8; ++i) {
		printf(" %02x", second[i]);
	}
	printf("\n");
}

// A command number the block does not have, which it has to refuse rather than
// wait on.
static void testUnknownCommand(void) {
	u8 answer[8];
	static const u8 commands[] = {0x00, 0x7F, 0xFE, 0xFF};
	unsigned i;

	printf("Command numbers the block does not have:\n");
	for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
		memset(answer, 0, sizeof(answer));
		printf("  %02x: %d bytes, ready %02x error %02x\n", commands[i],
		       sendReadOnlyCommand(commands[i], answer, 8), read8(CDVD_S_READY),
		       read8(CDVD_ERROR));
	}
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	dump("As the program found it");
	testReady();
	testStatus();
	testClockCommand();
	testRepeatedCommand();
	testUnknownCommand();
	dump("After the commands");

	printf("-- TEST END\n");
	return 1;
}
