#include "macro.h"
#include "../../vu/assemble.h"

// Starting the microprogram from the processor rather than through the vector
// interface, and what the status register says while it runs.

static const u16 markerQuadword = 0x30;

static volatile u32 *const markerAddress =
	(volatile u32 *)(vu0_mem + 16 * markerQuadword);

static u32 vpuStat() {
	u32 status;
	asm volatile ("cfc2 %0, $29\n" : "=r"(status));
	return status;
}

static u32 programCounter() {
	u32 value;
	asm volatile ("cfc2 %0, $26\n" : "=r"(value));
	return value;
}

// A program that leaves a number behind and ends, so the marker says it ran
// and the counter says where it stopped.
static u32 buildMarkerProgram(VU::LIW *program, int marker, int steps) {
	using namespace VU;

	Block block(program);
	for (int i = 0; i < steps; ++i) {
		block.Wr(IADDIU(VI01, VI00, marker + i));
		block.Wr(ISW(DEST_X, VI01, VI00, markerQuadword + i));
	}
	block.SafeExit();
	return steps * 2 + 2;
}

static void writeProgram(u16 atInstruction, const VU::LIW *program, u32 count) {
	VU::LIW *micro = vu0_micro + atInstruction;
	for (u32 i = 0; i < count; ++i) {
		micro[i] = program[i];
	}
}

static void clearMarkers() {
	for (int i = 0; i < 8; ++i) {
		markerAddress[i * 4] = 0;
	}
}

static void printMarkers(const char *what) {
	printf("  %-20s", what);
	for (int i = 0; i < 4; ++i) {
		printf(" %04x", markerAddress[i * 4] & 0xFFFF);
	}
	printf(" vpu %08x tpc %04x\n", vpuStat(), programCounter() & 0xFFFF);
}

// The immediate form, whose address counts double instructions.
#define CALL_AT(N) \
static void callAt##N() { \
	asm volatile ("vcallms " #N "\n" "sync.p\n"); \
}

// The immediate is a byte address, so it has to be a multiple of eight and
// the instruction it names is that over eight.
CALL_AT(0)
CALL_AT(8)
CALL_AT(16)
CALL_AT(32)

static void testAddresses() {
	typedef void (*CallFunction)();
	static const CallFunction calls[] = {&callAt0, &callAt8, &callAt16, &callAt32};
	static const u16 addresses[] = {0, 8, 16, 32};

	printf("vcallms at an address:\n");
	for (unsigned i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) {
		VU::LIW program[16];
		const u32 count = buildMarkerProgram(program, 0x100 + i, 2);
		memset(vu0_micro, 0, 0x1000);
		writeProgram(addresses[i] / 8, program, count);
		clearMarkers();

		calls[i]();

		char label[32];
		sprintf(label, "address %u", addresses[i]);
		printMarkers(label);
	}
}

// The register form, which takes its address from an integer register.
static void testRegisterForm() {
	static const u16 addresses[] = {0, 8, 16};

	printf("vcallmsr:\n");
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		VU::LIW program[16];
		const u32 count = buildMarkerProgram(program, 0x200 + i, 2);
		memset(vu0_micro, 0, 0x1000);
		writeProgram(addresses[i] / 8, program, count);
		clearMarkers();

		const u32 start = addresses[i];
		asm volatile (
			"ctc2 %0, $27\n"
			"sync.p\n"
			"vcallmsr $vi27\n"
			"sync.p\n"
			: : "r"(start)
		);

		char label[32];
		sprintf(label, "cmsar0 %u", addresses[i]);
		printMarkers(label);
	}
}

// What the status register says before, during and after, and whether a
// transfer that is not interlocked can see the unit busy.
static void testStatus() {
	VU::LIW program[64];
	const u32 count = buildMarkerProgram(program, 0x300, 8);
	memset(vu0_micro, 0, 0x1000);
	writeProgram(0, program, count);
	clearMarkers();

	printf("Status around a call:\n");
	const u32 before = vpuStat();
	u32 during = 0;
	asm volatile (
		"vcallms 0\n"
		"cfc2 %0, $29\n"
		: "=r"(during)
	);
	const u32 after = vpuStat();

	printf("  before %08x during %08x after %08x\n", before, during, after);
	printMarkers("markers");
}

// Two calls in a row, and a call while the unit is already running.
static void testRepeated() {
	VU::LIW program[64];
	const u32 count = buildMarkerProgram(program, 0x400, 4);
	memset(vu0_micro, 0, 0x1000);
	writeProgram(0, program, count);

	printf("Two calls in a row:\n");
	clearMarkers();
	asm volatile ("vcallms 0\n" "sync.p\n");
	printMarkers("after the first");
	clearMarkers();
	asm volatile ("vcallms 0\n" "sync.p\n");
	printMarkers("after the second");

	printf("A call with no wait between:\n");
	clearMarkers();
	asm volatile (
		"vcallms 0\n"
		"vcallms 0\n"
		"sync.p\n"
	);
	printMarkers("after both");
}

// The reset bit in the control register, which stops a running program.
static void testForceBreak() {
	printf("FBRST:\n");
	static const u32 bits[] = {0x0001, 0x0002, 0x0100, 0x0200};
	static const char *const names[] = {"vu0 reset", "vu0 break", "vu1 reset",
	                                    "vu1 break"};

	for (unsigned i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i) {
		asm volatile ("ctc2 %0, $28\n" "sync.p\n" : : "r"(bits[i]));
		printf("  %-12s vpu %08x\n", names[i], vpuStat());
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testAddresses();
	testRegisterForm();
	testStatus();
	testRepeated();
	testForceBreak();

	printf("-- TEST END\n");
	return 0;
}
