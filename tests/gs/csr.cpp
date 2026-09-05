#include <common-ee.h>
#include <ee_regs.h>
#include <kernel.h>
#include "../dma/dmaregs.h"
#include "../dma/dmasend.h"
#include "emit_giftag.h"
#include "gsregs.h"

// The status register, whose bits the unit raises and a program clears by
// writing a one back.  Two of them are raised by a packet, so those are
// measured against a packet rather than against a write.

static volatile u64 *const CSR = (volatile u64 *)0x12001000;
static volatile u64 *const IMR = (volatile u64 *)0x12001010;

static void printCsr(const char *what) {
	const u64 value = *CSR;
	printf("  %-28s %016llx  signal %d finish %d hsint %d vsint %d edwint %d "
	       "flush %d reset %d field %d fifo %d rev %02x id %02x\n",
	       what, value, (u32)(value & 1), (u32)((value >> 1) & 1),
	       (u32)((value >> 2) & 1), (u32)((value >> 3) & 1),
	       (u32)((value >> 4) & 1), (u32)((value >> 8) & 1),
	       (u32)((value >> 9) & 1), (u32)((value >> 12) & 1),
	       (u32)((value >> 14) & 3), (u32)((value >> 16) & 0xFF),
	       (u32)((value >> 24) & 0xFF));
}

// Which bits a write of one clears, taken one bit at a time.
static void testClearing() {
	printf("Writing a one to each of the low bits:\n");
	for (int bit = 0; bit < 16; ++bit) {
		const u64 before = *CSR;
		*CSR = 1ULL << bit;
		const u64 after = *CSR;
		printf("  bit %2d: %016llx then %016llx, %s\n", bit, before, after,
		       before == after ? "unchanged" : "changed");
	}
}

// The two bits that say what the picture is doing, sampled often enough that a
// running machine shows both values.
static void testFieldAndVsync() {
	printf("The field bit over sixteen reads:\n");
	printf("   ");
	for (int i = 0; i < 16; ++i) {
		printf(" %d", (u32)((*CSR >> 13) & 1));
		for (volatile int j = 0; j < 40000; ++j) {
			continue;
		}
	}
	printf("\n");

	printf("Waiting for the vertical interrupt bit:\n");
	*CSR = 1ULL << 3;
	int spins = 0;
	for (; spins < 4000000 && ((*CSR >> 3) & 1) == 0; ++spins) {
		continue;
	}
	printf("  raised after %d checks\n", spins);
	*CSR = 1ULL << 3;
	printf("  cleared, reads %d\n", (u32)((*CSR >> 3) & 1));
}

// The identity bits, which name the chip and never change.
static void testIdentity() {
	printf("The identity:\n");
	const u64 value = *CSR;
	printf("  revision %02x, id %02x\n", (u32)((value >> 16) & 0xFF),
	       (u32)((value >> 24) & 0xFF));
	printf("  read again: revision %02x, id %02x\n",
	       (u32)((*CSR >> 16) & 0xFF), (u32)((*CSR >> 24) & 0xFF));
}

// The bit a packet raises when the unit has finished everything before it.
static void testFinish() {
	printf("The finish bit:\n");

	*CSR = 2;
	printCsr("before the packet");

	GIF::Packet packet(256);
	GIF::Tag tag;
	tag.SetLoops(1);
	tag.SetEop();
	tag.SetFormat(GIF::FORMAT_PACKED);
	tag.SetRegDescs(GIF::REG_AD);
	packet.WriteTag(tag);
	packet.Emit(1);
	packet.Emit(0x61);
	DMA::SendSimple(DMA::D2, packet.Raw(), packet.Size());

	int spins = 0;
	for (; spins < 1000000 && ((*CSR >> 1) & 1) == 0; ++spins) {
		continue;
	}
	printf("  raised after %d checks\n", spins);
	printCsr("after the packet");

	*CSR = 2;
	printCsr("after clearing it");
	*CSR = 2;
	printCsr("after clearing it again");
}

// The bit a signal raises, which also holds the next packet until it is
// cleared.
static void testSignal() {
	printf("The signal bit:\n");

	*CSR = GS::CSR_SIGNAL;
	*GS::SIGLBLID = 0;

	GIF::Packet packet(256);
	GIF::Tag tag;
	tag.SetLoops(1);
	tag.SetEop();
	tag.SetFormat(GIF::FORMAT_PACKED);
	tag.SetRegDescs(GIF::REG_AD);
	packet.WriteTag(tag);
	packet.Emit(GS::SIGNAL(0x1234ABCD, 0xFFFFFFFF));
	packet.Emit(GS::REG_SIGNAL);
	DMA::SendSimple(DMA::D2, packet.Raw(), packet.Size());

	int spins = 0;
	for (; spins < 1000000 && (*CSR & 1) == 0; ++spins) {
		continue;
	}
	printf("  raised after %d checks, SIGLBLID %016llx\n", spins, *GS::SIGLBLID);
	printCsr("after the signal");

	*CSR = GS::CSR_SIGNAL;
	printCsr("after clearing it");
}

// The mask register, which decides which of those bits reach the processor.
static void testMask() {
	static const u64 values[] = {0x0000, 0xFF00, 0x1F00, 0x0100, 0xFFFFFFFFFFFFFFFFULL};

	printf("The interrupt mask:\n");
	const u64 saved = *IMR;
	printf("  as found %016llx\n", saved);
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		*IMR = values[i];
		printf("  wrote %016llx, reads %016llx\n", values[i], *IMR);
	}
	*IMR = saved;
	printf("  put back, reads %016llx\n", *IMR);
}

// The reset bit, which is a write with no readback.  Everything the display
// needs is saved first, since a reset takes the mode with it.
static void testReset() {
	printf("The reset bit:\n");

	static const u32 saved[] = {0x12000000, 0x12000010, 0x12000020, 0x12000030,
	                            0x12000040, 0x12000050, 0x12000060, 0x12000070,
	                            0x12000080, 0x12000090, 0x120000A0};
	u64 values[sizeof(saved) / sizeof(saved[0])];
	unsigned i;
	for (i = 0; i < sizeof(saved) / sizeof(saved[0]); ++i) {
		values[i] = *(volatile u64 *)saved[i];
	}
	const u64 mask = *IMR;

	*CSR = 1ULL << 9;
	printCsr("straight after a reset");

	for (i = 0; i < sizeof(saved) / sizeof(saved[0]); ++i) {
		*(volatile u64 *)saved[i] = values[i];
	}
	*IMR = mask;
	printCsr("with the mode put back");
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	printCsr("as the program found it");
	testIdentity();
	testMask();
	testClearing();
	testFinish();
	testSignal();
	testFieldAndVsync();
	testReset();

	printf("-- TEST END\n");
	return 0;
}
