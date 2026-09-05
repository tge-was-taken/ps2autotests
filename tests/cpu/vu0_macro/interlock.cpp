#include "macro.h"
#include "../../vu/assemble.h"

// The transfer instructions come in two forms, one that waits for the unit and
// one that does not.  With a program running, the two should disagree, and
// that disagreement is what a program relies on when it polls.

static const u16 markerQuadword = 0x30;

static volatile u32 *const markerAddress =
	(volatile u32 *)(vu0_mem + 16 * markerQuadword);

static u32 vpuStat() {
	u32 status;
	asm volatile ("cfc2 %0, $29\n" : "=r"(status));
	return status;
}

static u32 vpuStatInterlocked() {
	u32 status;
	asm volatile ("cfc2.i %0, $29\n" : "=r"(status));
	return status;
}

// A program long enough that the processor is still ahead of it after the
// call returns.
static u32 buildSlowProgram(VU::LIW *program, int steps) {
	using namespace VU;

	Block block(program);
	for (int i = 0; i < steps; ++i) {
		block.Wr(IADDIU(VI01, VI00, i + 1));
		block.Wr(ISW(DEST_X, VI01, VI00, markerQuadword));
	}
	block.SafeExit();
	return steps * 2 + 2;
}

static void writeProgram(const VU::LIW *program, u32 count) {
	for (u32 i = 0; i < count; ++i) {
		vu0_micro[i] = program[i];
	}
}

// The status register read both ways right after a call.
static void testStatusForms() {
	VU::LIW program[256];
	const u32 count = buildSlowProgram(program, 100);
	memset(vu0_micro, 0, 0x1000);
	writeProgram(program, count);
	markerAddress[0] = 0;

	printf("Reading the status register after a call:\n");

	u32 plain = 0;
	asm volatile (
		"vcallms 0\n"
		"cfc2 %0, $29\n"
		: "=r"(plain)
	);
	const u32 settled = vpuStat();
	printf("  plain read %08x, once settled %08x\n", plain, settled);

	markerAddress[0] = 0;
	u32 interlocked = 0;
	asm volatile (
		"vcallms 0\n"
		"cfc2.i %0, $29\n"
		: "=r"(interlocked)
	);
	printf("  interlocked read %08x, marker %04x\n", interlocked,
	       markerAddress[0] & 0xFFFF);
}

// A register transfer both ways while a program is writing that register.
static void testRegisterForms() {
	using namespace VU;

	VU::LIW program[256];
	Block block(program);
	for (int i = 0; i < 60; ++i) {
		block.Wr(IADDIU(VI01, VI00, i + 1));
	}
	block.Wr(MFIR(DEST_XYZW, VF10, VI01));
	block.SafeExit();
	const u32 count = 60 + 3;

	memset(vu0_micro, 0, 0x1000);
	writeProgram(program, count);

	printf("Reading a register a running program is writing:\n");

	Quad plain, interlocked;
	splat(plain, junkBits);
	splat(interlocked, junkBits);

	asm volatile (
		"vcallms 0\n"
		"sqc2 $vf10, 0(%0)\n"
		: : "r"(&plain) : "memory"
	);
	asm volatile ("sync.p\n");

	memset(vu0_micro, 0, 0x1000);
	writeProgram(program, count);
	asm volatile (
		"vcallms 0\n"
		"sqc2 $vf10, 0(%0)\n"
		: : "r"(&interlocked) : "memory"
	);
	asm volatile ("sync.p\n");

	Quad settled;
	splat(settled, junkBits);
	asm volatile ("sqc2 $vf10, 0(%0)\n" : : "r"(&settled) : "memory");

	printf("  plain  ");
	printQuad(plain);
	printf("\n  again  ");
	printQuad(interlocked);
	printf("\n  settled ");
	printQuad(settled);
	printf("\n");
}

// The two ways to write a control register, with a program running.
static void testWriteForms() {
	VU::LIW program[256];
	const u32 count = buildSlowProgram(program, 100);
	memset(vu0_micro, 0, 0x1000);
	writeProgram(program, count);

	printf("Writing a control register during a call:\n");

	u32 back = 0;
	asm volatile (
		"vcallms 0\n"
		"ctc2 %1, $21\n"
		"sync.p\n"
		"cfc2 %0, $21\n"
		: "=r"(back) : "r"(0x11112222)
	);
	printf("  plain write, reads back %08x\n", back);

	memset(vu0_micro, 0, 0x1000);
	writeProgram(program, count);
	asm volatile (
		"vcallms 0\n"
		"ctc2.i %1, $21\n"
		"sync.p\n"
		"cfc2 %0, $21\n"
		: "=r"(back) : "r"(0x33334444)
	);
	printf("  interlocked write, reads back %08x\n", back);
}

// With nothing running the two forms have nothing to disagree about, which is
// worth recording as the baseline.
static void testIdle() {
	printf("With nothing running:\n");
	printf("  plain %08x, interlocked %08x\n", vpuStat(), vpuStatInterlocked());
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testIdle();
	testStatusForms();
	testRegisterForms();
	testWriteForms();

	printf("-- TEST END\n");
	return 0;
}
