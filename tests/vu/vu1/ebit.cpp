#include <common-ee.h>
#include <kernel.h>
#include <string.h>
#include "../assemble.h"
#include "../../dma/dmaregs.h"
#include "../../dma/dmasend.h"
#include "../../dma/vif/emit_vifcode.h"
#include "../../dma/vif/vifregs.h"

static const u16 markerQuadword = 0x200;
static const int markerCount = 6;

static volatile u32 *const markerAddress =
	(volatile u32 *)(vu1_mem + 16 * markerQuadword);

static bool isVu1Running() {
	u32 stat;
	asm volatile ("cfc2 %0, $29" : "=r"(stat));
	return (stat & (1 << 8)) != 0;
}

static u32 vpuStat() {
	u32 stat;
	asm volatile ("cfc2 %0, $29" : "=r"(stat));
	return stat;
}

static void run(const VU::LIW *program, u32 instructions) {
	memset((void *)markerAddress, 0, 16 * markerCount);
	SyncDCache((void *)markerAddress, (void *)(markerAddress + 4 * markerCount));
	VIF::VIF1->fbrst = VIF::FBRST_RST;

	VIF::Packet packet(16 + instructions * 8 + 16);
	packet.MPG(0, instructions);
	packet.DataPtr(program, instructions * 8);
	packet.MSCAL(0);
	packet.NOP();
	packet.NOP();
	packet.NOP();

	const u32 bytes = (4 + instructions * 8 + 16 + 15) & ~15u;
	DMA::SendSimple(DMA::D1, packet.Raw(), bytes);
	for (int i = 0; i < 1000000 && isVu1Running(); ++i) {
		continue;
	}
	SyncDCache((void *)markerAddress, (void *)(markerAddress + 4 * markerCount));
}

static void printMarkers(const char *what) {
	printf("  %s:", what);
	for (int i = 0; i < markerCount; ++i) {
		printf(" %d", markerAddress[i * 4] & 0xFFFF);
	}
	printf(" vpu %08x\n", vpuStat());
}

// A store per step, so the markers say how many steps ran.
static u32 buildCountingProgram(VU::LIW *program, int ebitStep, VU::Flags extra) {
	using namespace VU;

	Block block(program);
	for (int step = 0; step < markerCount; ++step) {
		block.Wr(IADDIU(VI01, VI00, step + 1));
		const Flags flags = step == ebitStep ? extra : UPPER_NONE;
		block.Wr(NOP(flags), ISW(DEST_X, VI01, VI00, markerQuadword + step));
	}
	block.SafeExit();
	return markerCount * 2 + 2;
}

static void testEbitPosition() {
	printf("E bit on step N, markers after:\n");
	for (int step = 0; step < markerCount; ++step) {
		VU::LIW program[32];
		const u32 instructions = buildCountingProgram(program, step, VU::UPPER_E);
		run(program, instructions);

		char name[24];
		sprintf(name, "step %d", step);
		printMarkers(name);
	}
}

// Nothing sets the E bit, so the program only ends at the exit the assembler
// appends.
static void testNoEbit() {
	printf("No E bit before the exit:\n");
	VU::LIW program[32];
	const u32 instructions = buildCountingProgram(program, -1, VU::UPPER_NONE);
	run(program, instructions);
	printMarkers("all steps");
}

// The other three flag bits, which are documented as debug and interlock
// rather than as an end.
static void testOtherFlags() {
	static const VU::Flags flags[] = {VU::UPPER_M, VU::UPPER_D, VU::UPPER_T};
	static const char *const names[] = {"M", "D", "T"};

	printf("Flag bit on step 2:\n");
	for (unsigned f = 0; f < sizeof(flags) / sizeof(flags[0]); ++f) {
		VU::LIW program[32];
		const u32 instructions = buildCountingProgram(program, 2, flags[f]);
		run(program, instructions);
		printMarkers(names[f]);
	}
}

// MSCNT restarts a program where the last E bit stopped it.
static void testContinue() {
	using namespace VU;

	printf("MSCNT after an E bit at step 1:\n");
	VU::LIW program[32];
	const u32 instructions = buildCountingProgram(program, 1, UPPER_E);
	run(program, instructions);
	printMarkers("first run");

	VIF::Packet again(16);
	again.MSCNT(0);
	again.NOP();
	again.NOP();
	again.NOP();
	DMA::SendSimple(DMA::D1, again.Raw(), 16);
	for (int i = 0; i < 1000000 && isVu1Running(); ++i) {
		continue;
	}
	SyncDCache((void *)markerAddress, (void *)(markerAddress + 4 * markerCount));
	printMarkers("after MSCNT");
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testEbitPosition();
	testNoEbit();
	testOtherFlags();
	testContinue();

	printf("-- TEST END\n");
	return 0;
}
