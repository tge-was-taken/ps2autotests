#include <common-ee.h>
#include <kernel.h>
#include <string.h>
#include "../dmaregs.h"
#include "../dmasend.h"
#include "emit_vifcode.h"
#include "vifunits.h"
#include "../../vu/assemble.h"

// Starting a microprogram from the vector interface rather than the processor.
// The program leaves a number in memory, so the marker says which one ran and
// the counter says where it stopped.

static const u16 markerQuadword = 0x30;

static volatile u32 *const marker =
	(volatile u32 *)(0x1100C000 + 16 * markerQuadword);

static u32 vpuStat() {
	u32 status;
	asm volatile ("cfc2 %0, $29\n" : "=r"(status));
	return status;
}

static bool isRunning() {
	return (vpuStat() & 0x0100) != 0;
}

static void waitForIdle() {
	for (int i = 0; i < 100000 && isRunning(); ++i) {
		continue;
	}
}

static void clearMarkers() {
	for (int i = 0; i < 16; ++i) {
		marker[i * 4] = 0;
	}
	SyncDCache((void *)marker, (void *)(marker + 64));
}

// Each program writes its own number, one quadword further along than the one
// before, so a run that continued rather than restarted is visible.
static u32 buildMarkerProgram(VU::LIW *program, int number, int slot) {
	using namespace VU;

	Block block(program);
	block.Wr(IADDIU(VI01, VI00, number));
	block.Wr(ISW(DEST_X, VI01, VI00, markerQuadword + slot));
	block.SafeExit();
	return 4;
}

static void upload(u16 atInstruction, const VU::LIW *program, u32 count) {
	VIF::Packet packet(16 + count * 8 + 16);
	packet.MPG(atInstruction, count);
	packet.DataPtr(program, count * 8);
	packet.NOP();
	packet.NOP();
	packet.NOP();

	const u32 bytes = (4 + count * 8 + 12 + 15) & ~15u;
	DMA::SendSimple(VIF::Unit1.dmaChannel, packet.Raw(), bytes);
}

static void sendCall(void (VIF::Packet::*command)(u16, VIF::CmdFlags), u16 address) {
	VIF::Packet packet(64);
	(packet.*command)(address, VIF::CMD_NORMAL);
	packet.NOP();
	packet.NOP();
	packet.NOP();
	DMA::SendSimple(VIF::Unit1.dmaChannel, packet.Raw(), 16);
}

static void printMarkers(const char *what) {
	printf("  %-30s", what);
	for (int i = 0; i < 4; ++i) {
		printf(" %04x", marker[i * 4] & 0xFFFF);
	}
	printf("  stat %08x\n", vpuStat());
}

// One program at the start of micro memory, started by each of the three
// commands in turn.
static void testEachCommand() {
	VU::LIW program[8];
	const u32 count = buildMarkerProgram(program, 0x0101, 0);

	printf("Each command on a program at instruction zero:\n");

	clearMarkers();
	upload(0, program, count);
	sendCall(&VIF::Packet::MSCAL, 0);
	waitForIdle();
	printMarkers("MSCAL");

	clearMarkers();
	sendCall(&VIF::Packet::MSCALF, 0);
	waitForIdle();
	printMarkers("MSCALF");

	clearMarkers();
	sendCall(&VIF::Packet::MSCNT, 0);
	waitForIdle();
	printMarkers("MSCNT");
}

// The address the command names, which counts instructions rather than bytes.
static void testAddresses() {
	static const u16 addresses[] = {0, 1, 2, 4, 8, 0x100};

	printf("MSCAL at each address:\n");
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		VU::LIW program[8];
		const u32 count = buildMarkerProgram(program, 0x0200 + i, 0);

		clearMarkers();
		upload(addresses[i], program, count);
		sendCall(&VIF::Packet::MSCAL, addresses[i]);
		waitForIdle();

		char name[40];
		sprintf(name, "address %04x, wrote %04x", addresses[i], 0x0200 + i);
		printMarkers(name);
	}
}

// An address past the end of micro memory, which the unit has to fold or
// refuse.
static void testAddressPastTheEnd() {
	static const u16 addresses[] = {0x400, 0x7FF, 0x800, 0xFFFF};

	VU::LIW program[8];
	const u32 count = buildMarkerProgram(program, 0x0303, 0);
	upload(0, program, count);

	printf("MSCAL past the end of micro memory:\n");
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		clearMarkers();
		sendCall(&VIF::Packet::MSCAL, addresses[i]);
		waitForIdle();

		char name[40];
		sprintf(name, "address %04x", addresses[i]);
		printMarkers(name);
	}
}

// MSCNT after a program that ended, which is documented as continuing from
// where the last one stopped rather than from an address.
static void testContinue() {
	printf("MSCNT after a program that ended:\n");

	// Two programs one after the other, so continuing lands in the second.
	VU::LIW first[8];
	VU::LIW second[8];
	buildMarkerProgram(first, 0x0401, 0);
	buildMarkerProgram(second, 0x0402, 1);

	clearMarkers();
	upload(0, first, 4);
	upload(4, second, 4);

	sendCall(&VIF::Packet::MSCAL, 0);
	waitForIdle();
	printMarkers("after MSCAL at zero");

	sendCall(&VIF::Packet::MSCNT, 0);
	waitForIdle();
	printMarkers("after MSCNT");

	sendCall(&VIF::Packet::MSCNT, 0);
	waitForIdle();
	printMarkers("after MSCNT again");
}

// A call sent while the unit is still running the last one, which is where the
// interface has to wait rather than start over.
static void testCallWhileRunning() {
	printf("A second call before the first has finished:\n");

	// A program long enough that the second command arrives during it.
	VU::LIW program[64];
	{
		using namespace VU;
		Block block(program);
		block.Wr(IADDIU(VI01, VI00, 0x0501));
		for (int i = 0; i < 24; ++i) {
			block.Wr(IADDIU(VI02, VI00, i));
		}
		block.Wr(ISW(DEST_X, VI01, VI00, markerQuadword));
		block.SafeExit();
	}

	clearMarkers();
	upload(0, program, 28);

	VIF::Packet packet(64);
	packet.MSCAL(0);
	packet.MSCAL(0);
	packet.NOP();
	packet.NOP();
	DMA::SendSimple(VIF::Unit1.dmaChannel, packet.Raw(), 16);

	waitForIdle();
	printMarkers("two calls in one packet");
	printf("  VIF1 stat %08x, err %08x\n", VIF::Unit1.regs->stat.bits_,
	       VIF::Unit1.regs->err.bits_);
}

// The interface status while a program is running, which is what a program
// polls before it sends the next batch.
static void testStatusWhileRunning() {
	printf("The status around a call:\n");

	VU::LIW program[64];
	{
		using namespace VU;
		Block block(program);
		for (int i = 0; i < 40; ++i) {
			block.Wr(IADDIU(VI02, VI00, i));
		}
		block.Wr(IADDIU(VI01, VI00, 0x0601));
		block.Wr(ISW(DEST_X, VI01, VI00, markerQuadword));
		block.SafeExit();
	}

	clearMarkers();
	upload(0, program, 44);

	printf("  before: VPU_STAT %08x VIF1 stat %08x\n", vpuStat(),
	       VIF::Unit1.regs->stat.bits_);
	sendCall(&VIF::Packet::MSCAL, 0);
	printf("  just after: VPU_STAT %08x VIF1 stat %08x\n", vpuStat(),
	       VIF::Unit1.regs->stat.bits_);
	waitForIdle();
	printf("  once idle: VPU_STAT %08x VIF1 stat %08x\n", vpuStat(),
	       VIF::Unit1.regs->stat.bits_);
	printMarkers("marker");
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testEachCommand();
	testAddresses();
	testContinue();
	testAddressPastTheEnd();
	testCallWhileRunning();
	testStatusWhileRunning();

	printf("-- TEST END\n");
	return 0;
}
