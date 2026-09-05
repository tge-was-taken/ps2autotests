#include "shared.h"

// The wide multiply and divide units write the same two registers the narrow
// ones do, and the second pipeline has its own pair.  What a read sees at each
// distance behind an operation is a value rather than a time, so it
// reproduces.

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

static void readHiLo(HiLo &regs) {
	asm volatile (
		"mfhi %0\n"
		"mflo %1\n"
		"mfhi1 %2\n"
		"mflo1 %3\n"
		: "=r"(regs.hi), "=r"(regs.lo), "=r"(regs.hi1), "=r"(regs.lo1)
	);
}

static void seedHiLo(u64 value) {
	asm volatile (
		"mthi %0\n"
		"mtlo %0\n"
		"mthi1 %0\n"
		"mtlo1 %0\n"
		: : "r"(value)
	);
}

// The wide multiply, whose result spans both halves of both registers.
#define WIDE_GAP_FUNC(NAME, OP, GAP) \
static void NAME(const u32 *a, const u32 *b, HiLo &regs) { \
	register u128 rs, rt; \
	SET_M(rs, (u32 *)a); \
	SET_M(rt, (u32 *)b); \
	asm volatile ( \
		".set noreorder\n" \
		".set nomacro\n" \
		OP " $0, %4, %5\n" \
		GAP \
		"mfhi %0\n" \
		"mflo %1\n" \
		"mfhi1 %2\n" \
		"mflo1 %3\n" \
		".set macro\n" \
		".set reorder\n" \
		: "=&r"(regs.hi), "=&r"(regs.lo), "=&r"(regs.hi1), "=&r"(regs.lo1) \
		: "r"(rs), "r"(rt) \
	); \
}

WIDE_GAP_FUNC(pmulthGap0, "pmulth", "")
WIDE_GAP_FUNC(pmulthGap1, "pmulth", "nop\n")
WIDE_GAP_FUNC(pmulthGap4, "pmulth", "nop\nnop\nnop\nnop\n")
WIDE_GAP_FUNC(pmaddhGap0, "pmaddh", "")
WIDE_GAP_FUNC(pmaddhGap4, "pmaddh", "nop\nnop\nnop\nnop\n")
// The divide names only its two sources; the pair it writes is implied.
#define PAIR_GAP_FUNC(NAME, OP, GAP) \
static void NAME(const u32 *a, const u32 *b, HiLo &regs) { \
	register u128 rs, rt; \
	SET_M(rs, (u32 *)a); \
	SET_M(rt, (u32 *)b); \
	asm volatile ( \
		".set noreorder\n" \
		".set nomacro\n" \
		OP " %4, %5\n" \
		GAP \
		"mfhi %0\n" \
		"mflo %1\n" \
		"mfhi1 %2\n" \
		"mflo1 %3\n" \
		".set macro\n" \
		".set reorder\n" \
		: "=&r"(regs.hi), "=&r"(regs.lo), "=&r"(regs.hi1), "=&r"(regs.lo1) \
		: "r"(rs), "r"(rt) \
	); \
}

PAIR_GAP_FUNC(pdivwGap0, "pdivw", "")
PAIR_GAP_FUNC(pdivwGap4, "pdivw", "nop\nnop\nnop\nnop\n")
PAIR_GAP_FUNC(pdivwGap8, "pdivw", "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n")

typedef void (*WideFunction)(const u32 *, const u32 *, HiLo &);

static u32 __attribute__((aligned(16))) operandA[4] = {0x00010002, 0x00030004,
                                                       0x00050006, 0x00070008};
static u32 __attribute__((aligned(16))) operandB[4] = {0x000A000B, 0x000C000D,
                                                       0x000E000F, 0x00100011};

static void testWideGaps() {
	struct Case {
		const char *name;
		WideFunction run;
	};
	static const Case cases[] = {
		{"pmulth gap 0", &pmulthGap0}, {"pmulth gap 1", &pmulthGap1},
		{"pmulth gap 4", &pmulthGap4}, {"pmaddh gap 0", &pmaddhGap0},
		{"pmaddh gap 4", &pmaddhGap4}, {"pdivw  gap 0", &pdivwGap0},
		{"pdivw  gap 4", &pdivwGap4},  {"pdivw  gap 8", &pdivwGap8},
	};

	printf("A read behind a wide operation:\n");
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		HiLo regs;
		seedHiLo(0);
		cases[i].run(operandA, operandB, regs);
		printf("  %-13s ", cases[i].name);
		printHiLo(regs);
		printf("\n");
	}
}

// The moves that read the pair as one wide value.
static void testMoveFromHiLo() {
	static const u64 seeds[] = {0x0123456789ABCDEFull, 0xFFFFFFFFFFFFFFFFull, 0};

	printf("pmfhl after a seed:\n");
	for (unsigned i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
		register u128 lw, uw, slw, lh, sh;
		seedHiLo(seeds[i]);
		asm volatile (
			"pmfhl.lw %0\n"
			"pmfhl.uw %1\n"
			"pmfhl.slw %2\n"
			"pmfhl.lh %3\n"
			"pmfhl.sh %4\n"
			: "=r"(lw), "=r"(uw), "=r"(slw), "=r"(lh), "=r"(sh)
		);

		printf("  seed %016llx\n", seeds[i]);
		printf("    lw  "); PRINT_R(lw, true);
		printf("    uw  "); PRINT_R(uw, true);
		printf("    slw "); PRINT_R(slw, true);
		printf("    lh  "); PRINT_R(lh, true);
		printf("    sh  "); PRINT_R(sh, true);
	}
}

// A write to the pair immediately before a read of it.
static void testWriteThenRead() {
	static const u64 values[] = {0, 1, 0x123456789ABCDEFull, 0xFFFFFFFFFFFFFFFFull};

	printf("pmthl then pmfhl with no gap:\n");
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		register u128 source, back;
		u32 __attribute__((aligned(16))) words[4];
		words[0] = (u32)values[i];
		words[1] = (u32)(values[i] >> 32);
		words[2] = 0x11111111;
		words[3] = 0x22222222;
		SET_M(source, words);

		asm volatile (
			".set noreorder\n"
			".set nomacro\n"
			"pmthl.lw %1\n"
			"pmfhl.lw %0\n"
			".set macro\n"
			".set reorder\n"
			: "=&r"(back) : "r"(source)
		);
		printf("  %016llx -> ", values[i]);
		PRINT_R(back, true);
	}
}

// The two pipelines running at once, which is what the second one is for.
static void testBothPipelines() {
	printf("A wide operation against the second pipeline:\n");

	HiLo regs;
	register u128 rs, rt;
	SET_M(rs, operandA);
	SET_M(rt, operandB);

	seedHiLo(0);
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"pmulth $0, %4, %5\n"
		"mult1 $0, %6, %7\n"
		"nop\nnop\nnop\nnop\n"
		"mfhi %0\n"
		"mflo %1\n"
		"mfhi1 %2\n"
		"mflo1 %3\n"
		".set macro\n"
		".set reorder\n"
		: "=&r"(regs.hi), "=&r"(regs.lo), "=&r"(regs.hi1), "=&r"(regs.lo1)
		: "r"(rs), "r"(rt), "r"(0x1234), "r"(0x5678)
	);
	printf("  pmulth then mult1: ");
	printHiLo(regs);
	printf("\n");

	seedHiLo(0);
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"mult1 $0, %6, %7\n"
		"pmulth $0, %4, %5\n"
		"nop\nnop\nnop\nnop\n"
		"mfhi %0\n"
		"mflo %1\n"
		"mfhi1 %2\n"
		"mflo1 %3\n"
		".set macro\n"
		".set reorder\n"
		: "=&r"(regs.hi), "=&r"(regs.lo), "=&r"(regs.hi1), "=&r"(regs.lo1)
		: "r"(rs), "r"(rt), "r"(0x1234), "r"(0x5678)
	);
	printf("  mult1 then pmulth: ");
	printHiLo(regs);
	printf("\n");
}

// A shift whose amount comes from the shift register rather than the
// instruction, which the funnel test sets but never races.
static void testShiftAmount() {
	printf("mtsa immediately before a shift that uses it:\n");
	for (int amount = 0; amount < 16; amount += 5) {
		register u128 rd;
		u32 __attribute__((aligned(16))) pattern[4] = {0x11223344, 0x55667788,
		                                               0x99AABBCC, 0xDDEEFF00};
		register u128 rs;
		SET_M(rs, pattern);

		asm volatile (
			".set noreorder\n"
			".set nomacro\n"
			"mtsa %2\n"
			"qfsrv %0, %1, %1\n"
			".set macro\n"
			".set reorder\n"
			: "=&r"(rd) : "r"(rs), "r"(amount)
		);
		printf("  amount %2d: ", amount);
		PRINT_R(rd, true);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testWideGaps();
	testMoveFromHiLo();
	testWriteThenRead();
	testBothPipelines();
	testShiftAmount();

	printf("-- TEST END\n");
	return 0;
}
