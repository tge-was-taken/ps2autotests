#include <common-iop.h>
#include <intrman.h>
#include <sysclib.h>

// The system coprocessor on the input processor, which is a small R3000 one:
// no address translation, a three deep interrupt and mode stack, and the debug
// registers.  Everything that changes state is done with interrupts suspended
// and put back.

#define READ_COP0(NUMBER) \
	({ u32 value; asm volatile ("mfc0 %0, $" #NUMBER "\n" "nop\n" : "=r"(value)); value; })

#define WRITE_COP0(NUMBER, VALUE) \
	do { asm volatile ("mtc0 %0, $" #NUMBER "\n" "nop\n" : : "r"(VALUE)); } while (0)

static void testWhichExist(void) {
	printf("Every register the coprocessor answers for:\n");
	printf("   3 BPC     %08x\n", READ_COP0(3));
	printf("   5 BDA     %08x\n", READ_COP0(5));
	printf("   6 TAR     %08x\n", READ_COP0(6));
	printf("   7 DCIC    %08x\n", READ_COP0(7));
	printf("   8 BadVadr %08x\n", READ_COP0(8));
	printf("   9 BDAM    %08x\n", READ_COP0(9));
	printf("  11 BPCM    %08x\n", READ_COP0(11));
	printf("  12 Status  %08x\n", READ_COP0(12));
	printf("  13 Cause   %08x\n", READ_COP0(13));
	printf("  14 EPC     %08x\n", READ_COP0(14));
	printf("  15 PRId    %08x\n", READ_COP0(15));
}

// The status register taken apart, which is the interrupt and mode stack a
// return from an exception rotates.
static void testStatusFields(void) {
	const u32 status = READ_COP0(12);

	printf("Status %08x:\n", status);
	printf("  iec %d kuc %d iep %d kup %d ieo %d kuo %d\n", status & 1,
	       (status >> 1) & 1, (status >> 2) & 1, (status >> 3) & 1,
	       (status >> 4) & 1, (status >> 5) & 1);
	printf("  im %02x isc %d swc %d pz %d cm %d pe %d ts %d bev %d\n",
	       (status >> 8) & 0xFF, (status >> 16) & 1, (status >> 17) & 1,
	       (status >> 18) & 1, (status >> 19) & 1, (status >> 20) & 1,
	       (status >> 21) & 1, (status >> 22) & 1);
	printf("  re %d cu %x\n", (status >> 25) & 1, (status >> 28) & 0xF);
}

// The cause register, which names the last exception even when none is
// pending.
static void testCauseFields(void) {
	const u32 cause = READ_COP0(13);

	printf("Cause %08x:\n", cause);
	printf("  exccode %2d ip %02x sw %d ce %d bd %d\n", (cause >> 2) & 0x1F,
	       (cause >> 8) & 0xFF, (cause >> 8) & 3, (cause >> 28) & 3,
	       (cause >> 31) & 1);
}

// Which bits the status register keeps, written with interrupts already off.
static void testStatusWrites(void) {
	// A value that clears the mode bits or sets the ones the manual reserves
	// stops the processor, so what is swept is the interrupt mask against the
	// value the kernel left behind.
	static const u32 masks[] = {0x00000000, 0x0000FF00, 0x00000100, 0x0000FE00};
	unsigned i;
	int state = 0;

	printf("Writing the interrupt mask in the status register:\n");
	CpuSuspendIntr(&state);
	{
		const u32 saved = READ_COP0(12);
		for (i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
			const u32 attempt = (saved & ~0x0000FF00u) | masks[i];
			u32 back;
			WRITE_COP0(12, attempt);
			back = READ_COP0(12);
			WRITE_COP0(12, saved);
			printf("  wrote %08x, read %08x\n", attempt, back);
		}
		WRITE_COP0(12, saved);
	}
	CpuResumeIntr(state);
}

// The software interrupt bits in the cause register, which are the two a
// program can raise itself.
static void testSoftwareBits(void) {
	int state = 0;

	printf("The software interrupt bits:\n");
	CpuSuspendIntr(&state);
	{
		const u32 saved = READ_COP0(13);
		WRITE_COP0(13, 0x00000100);
		printf("  wrote 00000100, cause reads %08x\n", READ_COP0(13));
		WRITE_COP0(13, 0x00000200);
		printf("  wrote 00000200, cause reads %08x\n", READ_COP0(13));
		WRITE_COP0(13, 0x00000000);
		printf("  wrote 00000000, cause reads %08x\n", READ_COP0(13));
		WRITE_COP0(13, saved);
	}
	CpuResumeIntr(state);
}

// The debug registers, which have no effect until the control register turns
// them on and so are safe to write.
static void testDebugRegisters(void) {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x1234ABCD};
	unsigned i;
	const u32 savedBpc = READ_COP0(3);
	const u32 savedBda = READ_COP0(5);
	const u32 savedBpcm = READ_COP0(11);
	const u32 savedBdam = READ_COP0(9);

	printf("The debug registers:\n");
	for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		WRITE_COP0(3, values[i]);
		WRITE_COP0(5, values[i]);
		WRITE_COP0(11, values[i]);
		WRITE_COP0(9, values[i]);
		printf("  wrote %08x: bpc %08x bda %08x bpcm %08x bdam %08x\n", values[i],
		       READ_COP0(3), READ_COP0(5), READ_COP0(11), READ_COP0(9));
	}
	WRITE_COP0(3, savedBpc);
	WRITE_COP0(5, savedBda);
	WRITE_COP0(11, savedBpcm);
	WRITE_COP0(9, savedBdam);
}

// The control register that arms the debug registers, left disarmed.
static void testDebugControl(void) {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x80000000};
	unsigned i;
	const u32 saved = READ_COP0(7);

	printf("The debug control register:\n");
	for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		WRITE_COP0(7, values[i]);
		printf("  wrote %08x, reads %08x\n", values[i], READ_COP0(7));
		WRITE_COP0(7, saved);
	}
	WRITE_COP0(7, saved);
}

// Coprocessor numbers the processor does not have, which a move from has to
// answer somehow.
static void testMissingCoprocessors(void) {
	printf("The identity register read four times: %08x %08x %08x %08x\n",
	       READ_COP0(15), READ_COP0(15), READ_COP0(15), READ_COP0(15));
	printf("Register 0, 1, 2 and 4, which the manual leaves out:\n");
	printf("  %08x %08x %08x %08x\n", READ_COP0(0), READ_COP0(1), READ_COP0(2),
	       READ_COP0(4));
	printf("Registers 16 through 19:\n");
	printf("  %08x %08x %08x %08x\n", READ_COP0(16), READ_COP0(17),
	       READ_COP0(18), READ_COP0(19));
}

// What the suspend and resume pair do to the status register, which is the
// same stack seen through the kernel.
static void testSuspendEffect(void) {
	int state = 0;
	u32 before;
	u32 during;
	u32 after;

	before = READ_COP0(12);
	CpuSuspendIntr(&state);
	during = READ_COP0(12);
	CpuResumeIntr(state);
	after = READ_COP0(12);

	printf("Status around a suspend: %08x, %08x, %08x\n", before, during, after);
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testWhichExist();
	testStatusFields();
	testCauseFields();
	testSuspendEffect();
	testMissingCoprocessors();
	testStatusWrites();
	testSoftwareBits();
	testDebugRegisters();
	testDebugControl();

	printf("-- TEST END\n");
	return 1;
}
