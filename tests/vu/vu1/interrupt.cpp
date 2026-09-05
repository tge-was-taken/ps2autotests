#include <common-ee.h>
#include <kernel.h>
#include <string.h>
#include "../assemble.h"
#include "../../dma/dmaregs.h"
#include "../../dma/dmasend.h"
#include "../../dma/vif/emit_vifcode.h"
#include "../../dma/vif/vifregs.h"

// The T bit, which stops the unit and raises its interrupt rather than ending
// the program.  What is recorded is how far the program got, what the status
// says while it is stopped, and what starts it again.

static const u16 markerQuadword = 0x200;
static const int markerCount = 6;

static volatile u32 *const markerAddress =
	(volatile u32 *)(vu1_mem + 16 * markerQuadword);

static const int vu1Cause = 7;
static volatile u32 *const intcStat = (volatile u32 *)0x1000F000;
static volatile u32 *const intcMask = (volatile u32 *)0x1000F010;

static u32 vpuStat() {
	u32 stat;
	asm volatile ("cfc2 %0, $29" : "=r"(stat));
	return stat;
}

static bool isRunning() {
	return (vpuStat() & (1 << 8)) != 0;
}

static void printState(const char *what) {
	printf("  %-22s markers", what);
	for (int i = 0; i < markerCount; ++i) {
		printf(" %d", markerAddress[i * 4] & 0xFFFF);
	}
	printf("  vpu %08x cause %d\n", vpuStat(), (*intcStat >> vu1Cause) & 1);
}

static void clearMarkers() {
	memset((void *)markerAddress, 0, 16 * markerCount);
	SyncDCache((void *)markerAddress, (void *)(markerAddress + 4 * markerCount));
}

static void waitForIdle() {
	for (int i = 0; i < 1000000 && isRunning(); ++i) {
		continue;
	}
	SyncDCache((void *)markerAddress, (void *)(markerAddress + 4 * markerCount));
}

static void upload(const VU::LIW *program, u32 instructions) {
	VIF::Packet packet(16 + instructions * 8 + 16);
	packet.MPG(0, instructions);
	packet.DataPtr(program, instructions * 8);
	packet.NOP();
	packet.NOP();
	packet.NOP();

	const u32 bytes = (4 + instructions * 8 + 12 + 15) & ~15u;
	DMA::SendSimple(DMA::D1, packet.Raw(), bytes);
}

static void call(u16 address) {
	VIF::Packet packet(64);
	packet.MSCAL(address);
	packet.NOP();
	packet.NOP();
	packet.NOP();
	DMA::SendSimple(DMA::D1, packet.Raw(), 16);
}

static void resume() {
	VIF::Packet packet(64);
	packet.MSCNT(0);
	packet.NOP();
	packet.NOP();
	packet.NOP();
	DMA::SendSimple(DMA::D1, packet.Raw(), 16);
}

// A store per step, so the markers say where the unit stopped.
static u32 buildCountingProgram(VU::LIW *program, int bitStep, VU::Flags extra) {
	using namespace VU;

	Block block(program);
	for (int step = 0; step < markerCount; ++step) {
		block.Wr(IADDIU(VI01, VI00, step + 1));
		const Flags flags = step == bitStep ? extra : UPPER_NONE;
		block.Wr(NOP(flags), ISW(DEST_X, VI01, VI00, markerQuadword + step));
	}
	block.SafeExit();
	return markerCount * 2 + 2;
}

static void clearCause() {
	*intcStat = 1u << vu1Cause;
}

// Where the unit stops for a T bit on each step.
static void testStopPosition() {
	printf("T bit on step N:\n");
	for (int step = 0; step < markerCount; ++step) {
		VU::LIW program[32];
		const u32 instructions = buildCountingProgram(program, step, VU::UPPER_T);

		VIF::VIF1->fbrst = VIF::FBRST_RST;
		clearCause();
		clearMarkers();
		upload(program, instructions);
		call(0);
		waitForIdle();

		char name[24];
		sprintf(name, "step %d", step);
		printState(name);
		clearCause();
	}
}

// Starting the unit again after a T bit stopped it.
static void testResume() {
	printf("Starting again after a stop:\n");

	VU::LIW program[32];
	const u32 instructions = buildCountingProgram(program, 2, VU::UPPER_T);

	VIF::VIF1->fbrst = VIF::FBRST_RST;
	clearCause();
	clearMarkers();
	upload(program, instructions);
	call(0);
	waitForIdle();
	printState("after the stop");

	resume();
	waitForIdle();
	printState("after MSCNT");

	resume();
	waitForIdle();
	printState("after MSCNT again");
	clearCause();
}

// The T bit and the E bit on the same instruction, which asks the unit to stop
// and to end at once.
static void testWithEbit() {
	printf("T and E on the same instruction:\n");

	VU::LIW program[32];
	const u32 instructions = buildCountingProgram(
		program, 2, (VU::Flags)(VU::UPPER_T | VU::UPPER_E));

	VIF::VIF1->fbrst = VIF::FBRST_RST;
	clearCause();
	clearMarkers();
	upload(program, instructions);
	call(0);
	waitForIdle();
	printState("after the run");

	resume();
	waitForIdle();
	printState("after MSCNT");
	clearCause();
}

// The D bit, which stops the unit the same way but on its own line in the
// status register.
static void testDbit() {
	printf("D bit on step 2:\n");

	VU::LIW program[32];
	const u32 instructions = buildCountingProgram(program, 2, VU::UPPER_D);

	VIF::VIF1->fbrst = VIF::FBRST_RST;
	clearCause();
	clearMarkers();
	upload(program, instructions);
	call(0);
	waitForIdle();
	printState("after the run");

	resume();
	waitForIdle();
	printState("after MSCNT");
	clearCause();
}

// Whether the cause reaches the controller with its mask off, and whether the
// bit stays up until it is written.
static void testCauseAndMask() {
	printf("The cause the stop raises:\n");

	const u32 savedMask = *intcMask;

	VU::LIW program[32];
	const u32 instructions = buildCountingProgram(program, 1, VU::UPPER_T);

	VIF::VIF1->fbrst = VIF::FBRST_RST;
	clearCause();
	printf("  before: stat %08x mask %08x\n", *intcStat, *intcMask);

	clearMarkers();
	upload(program, instructions);
	call(0);
	waitForIdle();
	printf("  after:  stat %08x mask %08x\n", *intcStat, *intcMask);

	// Reading twice, since the bit is documented as staying up on its own.
	printf("  read again: stat %08x\n", *intcStat);
	clearCause();
	printf("  after clearing: stat %08x\n", *intcStat);
	clearCause();
	printf("  after clearing again: stat %08x\n", *intcStat);

	*intcMask = (*intcMask ^ savedMask) & 0xFFFF;
	resume();
	waitForIdle();
	clearCause();
}

// A reset while the unit is stopped, which is how a program recovers without
// running the rest of the microprogram.
static void testResetWhileStopped() {
	printf("A reset while the unit is stopped:\n");

	VU::LIW program[32];
	const u32 instructions = buildCountingProgram(program, 2, VU::UPPER_T);

	VIF::VIF1->fbrst = VIF::FBRST_RST;
	clearCause();
	clearMarkers();
	upload(program, instructions);
	call(0);
	waitForIdle();
	printState("stopped");

	VIF::VIF1->fbrst = VIF::FBRST_RST;
	printState("after a reset");

	call(0);
	waitForIdle();
	printState("after starting again");
	clearCause();
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testStopPosition();
	testResume();
	testWithEbit();
	testDbit();
	testCauseAndMask();
	testResetWhileStopped();

	printf("-- TEST END\n");
	return 0;
}
