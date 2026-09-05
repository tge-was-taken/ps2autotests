#include <common-ee.h>
#include <kernel.h>
#include <string.h>
#include "../assemble.h"
#include "../../dma/dmaregs.h"
#include "../../dma/dmasend.h"
#include "../../dma/vif/emit_vifcode.h"
#include "../../dma/vif/vifregs.h"

// Clear of anything BASE and OFFSET are set to below.
static const u16 resultQuadword = 0x300;

static volatile u32 *const resultAddress = (volatile u32 *)(vu1_mem + 16 * resultQuadword);

static bool isVu1Running() {
	u32 stat;
	asm volatile ("cfc2 %0, $29" : "=r"(stat));
	return (stat & (1 << 8)) != 0;
}

static void waitForVu1() {
	for (int i = 0; i < 1000000 && isVu1Running(); ++i) {
		continue;
	}
}

// Reads the two buffer registers the unit exposes to a microprogram and
// leaves them where the ee can see them.
static u32 buildProgram(VU::LIW *program) {
	using namespace VU;

	Block block(program);
	block.Wr(XTOP(VI01));
	block.Wr(XITOP(VI02));
	block.Wr(NOP());
	block.Wr(NOP());
	block.Wr(ISW(DEST_X, VI01, VI00, resultQuadword));
	block.Wr(ISW(DEST_X, VI02, VI00, resultQuadword + 1));
	block.SafeExit();
	return 8;
}

static void printState(const char *what) {
	SyncDCache((void *)resultAddress, (void *)(resultAddress + 8));
	printf("  %s: base %04x ofst %04x tops %04x top %04x itops %04x itop %04x"
	       " dbf %d xtop %04x xitop %04x\n",
	       what, VIF::VIF1->base, VIF::VIF1->ofst, VIF::VIF1->tops, VIF::VIF1->top,
	       VIF::VIF1->itops, VIF::VIF1->itop,
	       (VIF::VIF1->stat & VIF::STAT_DBF) ? 1 : 0,
	       resultAddress[0] & 0xFFFF, resultAddress[4] & 0xFFFF);
}

static void reset() {
	VIF::VIF1->fbrst = VIF::FBRST_RST;
	memset((void *)resultAddress, 0, 16 * 2);
	SyncDCache((void *)resultAddress, (void *)(resultAddress + 8));
}

// One packet sets the buffers up and runs the program; the ones after it only
// run the program, so the swap is the only thing that moved.
static void testSwap(u16 base, u16 offset, u16 itop) {
	VU::LIW program[8];
	const u32 instructions = buildProgram(program);

	reset();

	printf("BASE %04x OFFSET %04x ITOP %04x:\n", base, offset, itop);

	VIF::Packet setup(256);
	setup.BASE(base);
	setup.OFFSET(offset);
	setup.ITOP(itop);
	setup.MPG(0, instructions);
	setup.DataPtr(program, instructions * 8);
	setup.MSCAL(0);
	DMA::SendSimple(DMA::D1, setup.Raw(), 128);
	waitForVu1();
	printState("after setup");

	for (int i = 0; i < 3; ++i) {
		memset((void *)resultAddress, 0, 16 * 2);
		SyncDCache((void *)resultAddress, (void *)(resultAddress + 8));

		VIF::Packet again(16);
		again.MSCAL(0);
		again.NOP();
		again.NOP();
		again.NOP();
		DMA::SendSimple(DMA::D1, again.Raw(), 16);
		waitForVu1();

		char name[24];
		sprintf(name, "run %d", i + 2);
		printState(name);
	}
}

// ITOP is loaded from ITOPS when a program starts, so a change mid flight only
// shows up on the next one.
static void testItopLatch() {
	VU::LIW program[8];
	const u32 instructions = buildProgram(program);

	reset();
	printf("ITOP changing between runs:\n");

	VIF::Packet setup(256);
	setup.BASE(0);
	setup.OFFSET(0);
	setup.ITOP(0x11);
	setup.MPG(0, instructions);
	setup.DataPtr(program, instructions * 8);
	setup.MSCAL(0);
	DMA::SendSimple(DMA::D1, setup.Raw(), 128);
	waitForVu1();
	printState("itops 0011");

	static const u16 values[] = {0x22, 0x33, 0x3FF};
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		memset((void *)resultAddress, 0, 16 * 2);
		SyncDCache((void *)resultAddress, (void *)(resultAddress + 8));

		VIF::Packet again(16);
		again.ITOP(values[i]);
		again.MSCAL(0);
		again.NOP();
		again.NOP();
		DMA::SendSimple(DMA::D1, again.Raw(), 16);
		waitForVu1();

		char name[24];
		sprintf(name, "itops %04x", values[i]);
		printState(name);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testSwap(0x000, 0x000, 0x000);
	testSwap(0x000, 0x100, 0x010);
	testSwap(0x040, 0x080, 0x020);
	testSwap(0x3FF, 0x001, 0x3FF);
	testItopLatch();

	printf("-- TEST END\n");
	return 0;
}
