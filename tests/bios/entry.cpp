#include <common-ee.h>
#include <kernel.h>
#include <string.h>

// The state a program finds itself in.  The startup code has already run by
// the time this is reached, so what is recorded is what survives it: the
// arguments, the pointers a compiler assumes, the thread the code is on, and
// the memory layout around it.

extern void *_gp;
extern void *_stack;
extern void *_end;

static u32 readGp() {
	u32 value;
	asm volatile ("move %0, $gp\n" : "=r"(value));
	return value;
}

static u32 readStackPointer() {
	u32 value;
	asm volatile ("move %0, $sp\n" : "=r"(value));
	return value;
}

// A stack address moves with the allocation, so only its region is printed.
static const char *region(u32 address) {
	if (address == 0) {
		return "null";
	}
	if (address < 0x00100000) {
		return "kernel";
	}
	if (address < 0x02000000) {
		return "main memory";
	}
	if (address >= 0x70000000 && address < 0x70004000) {
		return "scratchpad";
	}
	if (address >= 0x80000000 && address < 0xA0000000) {
		return "cached window";
	}
	if (address >= 0xA0000000) {
		return "uncached window";
	}
	return "elsewhere";
}

static void testArguments(int argc, char *argv[]) {
	printf("Arguments: argc %d, argv %s\n", argc, argv == 0 ? "null" : "set");
	for (int i = 0; i < argc && i < 4; ++i) {
		printf("  argv[%d] %s\n", i, argv[i] == 0 ? "null" : argv[i]);
	}
}

static void testPointers() {
	const u32 gp = readGp();
	const u32 sp = readStackPointer();

	printf("Pointers: gp in %s, sp in %s\n", region(gp), region(sp));
	printf("  gp matches the linker symbol: %s\n",
	       gp == (u32)&_gp ? "yes" : "no");
}

static void testThread() {
	const s32 id = GetThreadId();
	ee_thread_status_t status;
	memset(&status, 0, sizeof(status));
	const s32 result = ReferThreadStatus(id, &status);

	printf("Thread: refer %d, status %d, priority %d, current priority %d\n", result,
	       status.status, status.initial_priority, status.current_priority);
	printf("  entry in %s, stack in %s, wait type %d\n", region((u32)status.func),
	       region((u32)status.stack), status.waitType);
}

// The heap the startup code set up, and how much of memory is left over it.
static void testHeap() {
	void *const end = EndOfHeap();
	printf("Heap ends in %s\n", region((u32)end));
	printf("  above the program image: %s\n", (u32)end >= (u32)&_end ? "yes" : "no");
}

// The kernel occupies the bottom of memory, so a read there says whether it is
// present and what it starts with.
static void testKernelArea() {
	static const u32 addresses[] = {0x00000000, 0x00000080, 0x00000180, 0x00001000,
	                                0x00010000, 0x00080000};

	printf("The first word at each kernel address:\n");
	for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		const volatile u32 *at = (const volatile u32 *)(addresses[i] | 0x80000000);
		printf("  %08x: %08x %08x\n", addresses[i], at[0], at[1]);
	}
}

// The exception vectors, whose contents say whether a handler is installed.
static void testVectors() {
	static const u32 vectors[] = {0x80000000, 0x80000080, 0x80000100, 0x80000180,
	                              0x80000200};
	static const char *const names[] = {"reset", "tlb refill", "counter",
	                                    "common", "interrupt"};

	printf("Exception vectors:\n");
	for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
		const volatile u32 *at = (const volatile u32 *)vectors[i];
		printf("  %-11s %08x: %08x %08x, %s\n", names[i], vectors[i], at[0], at[1],
		       at[0] == 0 ? "empty" : "installed");
	}
}

// How much memory answers, which a devkit and a retail machine disagree about.
static void testMemorySize() {
	static const u32 megabytes[] = {4, 8, 16, 24, 30, 31, 32};

	printf("Writing near the top of each size:\n");
	for (unsigned i = 0; i < sizeof(megabytes) / sizeof(megabytes[0]); ++i) {
		const u32 at = (megabytes[i] * 1024 * 1024) - 16;
		volatile u32 *const uncached = (volatile u32 *)(at | 0xA0000000);
		volatile u32 *const low = (volatile u32 *)(0xA0000000 + 0x00200000);

		*low = 0x600D0000 + i;
		*uncached = 0xBAD00000 + i;
		printf("  %2u MB: wrote %08x reads %08x, low word %08x\n", megabytes[i],
		       0xBAD00000 + i, *uncached, *low);
	}
}

// Which window a write through one shows up in through the others.
static void testWindows() {
	static u32 __attribute__((aligned(64))) target = 0;
	const u32 physical = (u32)&target & 0x1FFFFFFF;

	volatile u32 *const cached = (volatile u32 *)(physical | 0x80000000);
	volatile u32 *const uncached = (volatile u32 *)(physical | 0xA0000000);
	volatile u32 *const direct = (volatile u32 *)physical;

	printf("One word through three windows:\n");
	*uncached = 0x11111111;
	FlushCache(0);
	printf("  wrote uncached: direct %08x cached %08x uncached %08x\n", *direct,
	       *cached, *uncached);

	*cached = 0x22222222;
	FlushCache(0);
	printf("  wrote cached:   direct %08x cached %08x uncached %08x\n", *direct,
	       *cached, *uncached);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testArguments(argc, argv);
	testPointers();
	testThread();
	testHeap();
	testKernelArea();
	testVectors();
	testMemorySize();
	testWindows();


	printf("-- TEST END\n");
	return 0;
}
