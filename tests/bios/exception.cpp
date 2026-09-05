#include <common-ee.h>
#include <kernel.h>
#include <string.h>

// The exception vectors and the tables behind them.  A handler installed
// wrongly loops forever with no way out, so nothing here installs one: what is
// recorded is the code the kernel put at each vector and the state a program
// can read about it.

struct Vector {
	const char *name;
	u32 address;
};

static const Vector vectors[] = {
	{"reset            ", 0xBFC00000},
	{"tlb refill       ", 0x80000000},
	{"counter          ", 0x80000080},
	{"debug            ", 0x80000100},
	{"common           ", 0x80000180},
	{"interrupt        ", 0x80000200},
	{"tlb refill, boot ", 0xBFC00200},
	{"common, boot     ", 0xBFC00380},
	{"interrupt, boot  ", 0xBFC00400},
};
static const int vectorCount = sizeof(vectors) / sizeof(vectors[0]);

static void testVectorContents() {
	printf("The first four words at each vector:\n");
	for (int i = 0; i < vectorCount; ++i) {
		const volatile u32 *const at = (const volatile u32 *)vectors[i].address;
		printf("  %s %08x: %08x %08x %08x %08x\n", vectors[i].name,
		       vectors[i].address, at[0], at[1], at[2], at[3]);
	}
}

// The bit that decides whether the vectors are taken from the boot area or
// from memory.
static void testBootVectorBit() {
	u32 status;
	asm volatile ("mfc0 %0, $12\n" "sync.p\n" : "=r"(status));
	printf("Status %08x, bev %d, so the vectors in use are the %s ones\n", status,
	       (status >> 22) & 1, ((status >> 22) & 1) ? "boot" : "memory");
}

// What the cause and program counter registers hold with no exception pending,
// which is the state a handler would see on entry if one were taken now.
static void testCauseAndCounter() {
	u32 cause;
	u32 counter;
	u32 errorCounter;
	u32 badAddress;

	asm volatile ("mfc0 %0, $13\n" "sync.p\n" : "=r"(cause));
	asm volatile ("mfc0 %0, $14\n" "sync.p\n" : "=r"(counter));
	asm volatile ("mfc0 %0, $30\n" "sync.p\n" : "=r"(errorCounter));
	asm volatile ("mfc0 %0, $8\n" "sync.p\n" : "=r"(badAddress));

	printf("Cause %08x: code %2d ip %02x bd %d bd2 %d ce %d\n", cause,
	       (cause >> 2) & 0x1F, (cause >> 8) & 0xFF, (cause >> 31) & 1,
	       (cause >> 30) & 1, (cause >> 28) & 3);
	printf("EPC %08x, ErrorEPC %08x, BadVAddr %08x\n", counter, errorCounter,
	       badAddress);
}

// The performance counters, which a handler on the counter vector is there to
// service.
static void testPerformanceCounters() {
	u32 control;
	asm volatile (".word 0x4001E800\n" "sync.p\n" : "=r"(control));
	printf("The performance control register reads %08x\n", control);
}

// Which of the kernel's own entry points answer, since a program that installs
// a handler goes through these.
static void testKernelCalls() {
	printf("The kernel's handler calls:\n");
	printf("  GetThreadId %d\n", GetThreadId());
	printf("  GetGsHParam and GetGsVParam are not read here\n");

	// Installing a null handler is the one call that cannot loop, since the
	// kernel has nothing to jump to and puts its own back.
	SetVCommonHandler(0, 0);
	printf("  SetVCommonHandler(0, 0) returned\n");
	SetVInterruptHandler(0, 0);
	printf("  SetVInterruptHandler(0, 0) returned\n");
	SetVTLBRefillHandler(0, 0);
	printf("  SetVTLBRefillHandler(0, 0) returned\n");
}

// The words at each vector read again after those calls, so a call that
// changed one shows up.
static void testVectorsAfter() {
	printf("The vectors again:\n");
	for (int i = 0; i < vectorCount; ++i) {
		const volatile u32 *const at = (const volatile u32 *)vectors[i].address;
		printf("  %s %08x %08x\n", vectors[i].name, at[0], at[1]);
	}
}

// The area between the vectors, which is where the kernel keeps the tables its
// handlers walk.
static void testHandlerArea() {
	printf("The kernel's low memory:\n");
	for (u32 at = 0x80000000; at < 0x80000600; at += 0x40) {
		const volatile u32 *const words = (const volatile u32 *)at;
		printf("  %08x: %08x %08x %08x %08x\n", at, words[0], words[1], words[2],
		       words[3]);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testBootVectorBit();
	testCauseAndCounter();
	testPerformanceCounters();
	testVectorContents();
	testHandlerArea();
	testKernelCalls();
	testVectorsAfter();

	printf("-- TEST END\n");
	return 0;
}
