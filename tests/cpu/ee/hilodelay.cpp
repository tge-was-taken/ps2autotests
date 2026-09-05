#include "shared.h"

// The multiply and divide units write hi and lo some cycles after they issue.
// Nothing here prints a cycle count: what it records is the value a read sees
// at each distance behind the operation, which is a value rather than a time.

struct HiLo {
	u64 hi;
	u64 lo;
	u64 hi1;
	u64 lo1;
};

static void printHiLo(const HiLo &regs) {
	printf("hi %016llx lo %016llx hi1 %016llx lo1 %016llx", regs.hi, regs.lo,
	       regs.hi1, regs.lo1);
}

static void setHiLo() {
	asm volatile (
		"mthi $0\n"
		"mtlo $0\n"
		"mthi1 $0\n"
		"mtlo1 $0\n"
	);
}

// A read placed a fixed number of instructions after the multiply.
#define MULT_GAP_FUNC(N, GAP) \
static u64 multGap##N(u32 a, u32 b) { \
	u64 result = 0; \
	asm volatile ( \
		".set noreorder\n" \
		".set nomacro\n" \
		"mthi $0\n" \
		"mult $0, %1, %2\n" \
		GAP \
		"mfhi %0\n" \
		".set macro\n" \
		".set reorder\n" \
		: "=&r"(result) : "r"(a), "r"(b) \
	); \
	return result; \
}

MULT_GAP_FUNC(0, "")
MULT_GAP_FUNC(1, "nop\n")
MULT_GAP_FUNC(2, "nop\nnop\n")
MULT_GAP_FUNC(3, "nop\nnop\nnop\n")
MULT_GAP_FUNC(4, "nop\nnop\nnop\nnop\n")

#define DIV_GAP_FUNC(N, GAP) \
static u64 divGap##N(u32 a, u32 b) { \
	u64 result = 0; \
	asm volatile ( \
		".set noreorder\n" \
		".set nomacro\n" \
		"mtlo $0\n" \
		"div $0, %1, %2\n" \
		GAP \
		"mflo %0\n" \
		".set macro\n" \
		".set reorder\n" \
		: "=&r"(result) : "r"(a), "r"(b) \
	); \
	return result; \
}

DIV_GAP_FUNC(0, "")
DIV_GAP_FUNC(1, "nop\n")
DIV_GAP_FUNC(2, "nop\nnop\n")
DIV_GAP_FUNC(3, "nop\nnop\nnop\nnop\n")
DIV_GAP_FUNC(4, "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n")

static void testMultiplyGaps() {
	typedef u64 (*GapFunction)(u32, u32);
	static const GapFunction runs[] = {&multGap0, &multGap1, &multGap2, &multGap3,
	                                   &multGap4};
	static const char *const names[] = {"0", "1", "2", "3", "4"};

	printf("mfhi after mult 0x12345678 by 0x9ABCDEF0:\n");
	for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
		printf("  gap %s: %016llx\n", names[i], runs[i](0x12345678, 0x9ABCDEF0));
	}
}

static void testDivideGaps() {
	typedef u64 (*GapFunction)(u32, u32);
	static const GapFunction runs[] = {&divGap0, &divGap1, &divGap2, &divGap3,
	                                   &divGap4};
	static const char *const names[] = {"0", "1", "2", "4", "8"};

	printf("mflo after div 1000 by 7:\n");
	for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
		printf("  gap %s: %016llx\n", names[i], runs[i](1000, 7));
	}
}

// The second pipeline has its own hi and lo, so an operation on one should
// leave the other alone.
static void testPipelineSeparation() {
	HiLo regs;

	setHiLo();
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"mult $0, %4, %5\n"
		"nop\nnop\nnop\nnop\n"
		"mfhi %0\n"
		"mflo %1\n"
		"mfhi1 %2\n"
		"mflo1 %3\n"
		".set macro\n"
		".set reorder\n"
		: "=&r"(regs.hi), "=&r"(regs.lo), "=&r"(regs.hi1), "=&r"(regs.lo1)
		: "r"(0x12345678), "r"(0x9ABCDEF0)
	);
	printf("after mult:  ");
	printHiLo(regs);
	printf("\n");

	setHiLo();
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"mult1 $0, %4, %5\n"
		"nop\nnop\nnop\nnop\n"
		"mfhi %0\n"
		"mflo %1\n"
		"mfhi1 %2\n"
		"mflo1 %3\n"
		".set macro\n"
		".set reorder\n"
		: "=&r"(regs.hi), "=&r"(regs.lo), "=&r"(regs.hi1), "=&r"(regs.lo1)
		: "r"(0x12345678), "r"(0x9ABCDEF0)
	);
	printf("after mult1: ");
	printHiLo(regs);
	printf("\n");
}

// Both pipelines busy at once, which is what the second one exists for.
static void testInterleaved() {
	HiLo regs;

	setHiLo();
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"mult $0, %4, %5\n"
		"mult1 $0, %5, %4\n"
		"nop\nnop\nnop\nnop\n"
		"mfhi %0\n"
		"mflo %1\n"
		"mfhi1 %2\n"
		"mflo1 %3\n"
		".set macro\n"
		".set reorder\n"
		: "=&r"(regs.hi), "=&r"(regs.lo), "=&r"(regs.hi1), "=&r"(regs.lo1)
		: "r"(0x12345678), "r"(0x9ABCDEF0)
	);
	printf("mult then mult1: ");
	printHiLo(regs);
	printf("\n");

	setHiLo();
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"div $0, %4, %5\n"
		"mult1 $0, %4, %5\n"
		"nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
		"mfhi %0\n"
		"mflo %1\n"
		"mfhi1 %2\n"
		"mflo1 %3\n"
		".set macro\n"
		".set reorder\n"
		: "=&r"(regs.hi), "=&r"(regs.lo), "=&r"(regs.hi1), "=&r"(regs.lo1)
		: "r"(1000), "r"(7)
	);
	printf("div then mult1:  ");
	printHiLo(regs);
	printf("\n");
}

// A write followed immediately by a read, which the pipeline has to forward.
static void testWriteThenRead() {
	static const u64 values[] = {0, 1, 0xFFFFFFFFull, 0x123456789ABCDEFull,
	                             0xFFFFFFFFFFFFFFFFull};

	printf("mthi then mfhi:\n");
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		u64 back = 0;
		asm volatile (
			".set noreorder\n"
			".set nomacro\n"
			"mthi %1\n"
			"mfhi %0\n"
			".set macro\n"
			".set reorder\n"
			: "=&r"(back) : "r"(values[i])
		);
		printf("  %016llx -> %016llx\n", values[i], back);
	}
}

// An accumulate reads hi and lo as well as writing them.
static void testAccumulate() {
	static const u64 seeds[] = {0, 1, 0xFFFFFFFFFFFFFFFFull};

	printf("madd on top of a seed:\n");
	for (unsigned i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
		HiLo regs;
		asm volatile (
			".set noreorder\n"
			".set nomacro\n"
			"mthi %4\n"
			"mtlo %4\n"
			"madd $0, %5, %6\n"
			"nop\nnop\nnop\nnop\n"
			"mfhi %0\n"
			"mflo %1\n"
			"mfhi1 %2\n"
			"mflo1 %3\n"
			".set macro\n"
			".set reorder\n"
			: "=&r"(regs.hi), "=&r"(regs.lo), "=&r"(regs.hi1), "=&r"(regs.lo1)
			: "r"(seeds[i]), "r"(0x10000), "r"(0x10000)
		);
		printf("  seed %016llx: ", seeds[i]);
		printHiLo(regs);
		printf("\n");
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testMultiplyGaps();
	testDivideGaps();
	testPipelineSeparation();
	testInterleaved();
	testWriteThenRead();
	testAccumulate();

	printf("-- TEST END\n");
	return 0;
}
