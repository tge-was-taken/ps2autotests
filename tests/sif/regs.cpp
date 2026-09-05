#include <common-ee.h>
#include <ee_regs.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <string.h>

// The registers the two processors talk through.  The link this program's
// output travels over runs on the same registers, so nothing here writes a bit
// the protocol uses: the two flag registers are only written with values that
// cannot change them, and the rest is read.

struct Named {
	const char *name;
	u32 address;
};

static const Named registers[] = {
	{"MADDR ", A_EE_SBUS_MADDR},
	{"SADDR ", A_EE_SBUS_SADDR},
	{"MSFLAG", A_EE_SBUS_MSFLAG},
	{"SMFLAG", A_EE_SBUS_SMFLAG},
	{"REG40 ", A_EE_SBUS_REG40},
	{"REG50 ", A_EE_SBUS_REG50},
	{"REG60 ", A_EE_SBUS_REG60},
	{"REG70 ", A_EE_SBUS_REG70},
	{"REG80 ", A_EE_SBUS_REG80},
	{"REG90 ", A_EE_SBUS_REG90},
	{"REGA0 ", A_EE_SBUS_REGA0},
	{"REGB0 ", A_EE_SBUS_REGB0},
	{"REGC0 ", A_EE_SBUS_REGC0},
	{"REGD0 ", A_EE_SBUS_REGD0},
	{"REGE0 ", A_EE_SBUS_REGE0},
	{"REGF0 ", A_EE_SBUS_REGF0},
};
static const int registerCount = sizeof(registers) / sizeof(registers[0]);

static u32 read(u32 address) {
	return *(volatile u32 *)address;
}

static void testEachRegister() {
	printf("Every register in the block:\n");
	for (int i = 0; i < registerCount; ++i) {
		printf("  %s at %08x: %08x\n", registers[i].name, registers[i].address,
		       read(registers[i].address));
	}
}

// A register that changes between two reads is one the other processor is
// driving rather than one this one owns.
static void testWhichChange() {
	u32 first[registerCount];
	u32 second[registerCount];

	for (int i = 0; i < registerCount; ++i) {
		first[i] = read(registers[i].address);
	}
	for (volatile int i = 0; i < 400000; ++i) {
		continue;
	}
	for (int i = 0; i < registerCount; ++i) {
		second[i] = read(registers[i].address);
	}

	printf("Reading twice with a wait in between:\n");
	for (int i = 0; i < registerCount; ++i) {
		printf("  %s %08x then %08x, %s\n", registers[i].name, first[i], second[i],
		       first[i] == second[i] ? "still" : "moved");
	}
}

// The flag registers written with a value that cannot change them, which says
// which way each one combines without touching a bit the protocol owns.
static void testFlagsWithNoChange() {
	printf("Writing the flag registers with a value that changes nothing:\n");

	const u32 mainBefore = read(A_EE_SBUS_MSFLAG);
	const u32 subBefore = read(A_EE_SBUS_SMFLAG);

	*(volatile u32 *)A_EE_SBUS_MSFLAG = 0;
	printf("  MSFLAG written zero: %08x then %08x, %s\n", mainBefore,
	       read(A_EE_SBUS_MSFLAG),
	       read(A_EE_SBUS_MSFLAG) == mainBefore ? "unchanged" : "changed");

	*(volatile u32 *)A_EE_SBUS_SMFLAG = 0;
	printf("  SMFLAG written zero: %08x then %08x, %s\n", subBefore,
	       read(A_EE_SBUS_SMFLAG),
	       read(A_EE_SBUS_SMFLAG) == subBefore ? "unchanged" : "changed");

	// Writing back exactly what is there says which way the register combines:
	// a register that keeps bits is unchanged, one that clears them empties.
	const u32 mainNow = read(A_EE_SBUS_MSFLAG);
	*(volatile u32 *)A_EE_SBUS_MSFLAG = mainNow;
	printf("  MSFLAG written its own value: %08x\n", read(A_EE_SBUS_MSFLAG));
}

// The control register the protocol uses to say a packet is waiting, read
// often enough that a change from the other side shows up.
static void testControlRegister() {
	printf("The control register over twelve reads:\n");
	printf("   ");
	for (int i = 0; i < 12; ++i) {
		printf(" %08x", read(A_EE_SBUS_REG40));
		for (volatile int j = 0; j < 20000; ++j) {
			continue;
		}
	}
	printf("\n");
}

// What the kernel's own calls report, which is the same state through the
// interface a program is meant to use.
static void testKernelView() {
	printf("Through the kernel's calls:\n");
	for (u32 number = 0; number < 6; ++number) {
		printf("  SifGetReg %u: %08x\n", number, SifGetReg(number));
	}
	printf("  SifIopSync %d\n", SifIopSync());
}

// The addresses either side puts its command buffer at, which is what a
// program reads to find the other's queue.
static void testCommandBuffers() {
	printf("The command buffer addresses:\n");
	printf("  main to sub %08x\n", read(A_EE_SBUS_MADDR));
	printf("  sub to main %08x\n", read(A_EE_SBUS_SADDR));
	printf("  the sub address is in the other memory: %s\n",
	       read(A_EE_SBUS_SADDR) < 0x00200000 ? "yes" : "no");
}

// The two channels the transfers run on, read rather than started.
static void testChannels() {
	static const u32 channels[] = {0x1000C000, 0x1000C400};
	static const char *const names[] = {"SIF0 in ", "SIF1 out"};

	printf("The two channels:\n");
	for (unsigned i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i) {
		volatile u32 *const channel = (volatile u32 *)channels[i];
		printf("  %s chcr %08x madr %08x qwc %08x tadr %08x\n", names[i],
		       channel[0], channel[4], channel[8], channel[12]);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testEachRegister();
	testCommandBuffers();
	testChannels();
	testKernelView();
	testControlRegister();
	testWhichChange();
	testFlagsWithNoChange();

	printf("-- TEST END\n");
	return 0;
}
