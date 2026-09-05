#include <common-ee.h>

// The short loop the processor manual warns about, where a loop body of one or
// two instructions ending in a branch is fetched wrongly unless it is padded.
// Each loop below counts a fixed number of iterations, so a count that is not
// the one asked for is the erratum showing.

#define COUNT_LOOP(BODY) \
	do { \
		u32 iterations, sum; \
		asm volatile ( \
			".set noreorder\n" \
			".set nomacro\n" \
			"li $8, 0\n" \
			"li $9, 0\n" \
			"1:\n" \
			"addiu $8, $8, 1\n" \
			BODY \
			"sltiu $10, $8, 16\n" \
			"bnez $10, 1b\n" \
			"addiu $9, $9, 3\n" \
			"move %0, $8\n" \
			"move %1, $9\n" \
			".set reorder\n" \
			: "=&r"(iterations), "=&r"(sum) : : "$8", "$9", "$10", "memory" \
		); \
		printf("  %-28s iterations %2d, sum %3d\n", name, iterations, sum); \
	} while (0)

static void testBodyLengths() {
	printf("A counting loop with a body of each length:\n");
	{ const char *name = "nothing before the test"; COUNT_LOOP(""); }
	{ const char *name = "one instruction";         COUNT_LOOP("nop\n"); }
	{ const char *name = "two instructions";        COUNT_LOOP("nop\nnop\n"); }
	{ const char *name = "three instructions";      COUNT_LOOP("nop\nnop\nnop\n"); }
	{ const char *name = "four instructions";       COUNT_LOOP("nop\nnop\nnop\nnop\n"); }
	{ const char *name = "eight instructions";
	  COUNT_LOOP("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"); }
}

// The tightest form the manual names: a branch back to itself with only its
// delay slot in the loop.
static void testTwoInstructionLoop() {
	u32 iterations;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"li $8, 0\n"
		"1:\n"
		"bne $8, %1, 1b\n"
		"addiu $8, $8, 1\n"
		"move %0, $8\n"
		".set reorder\n"
		: "=&r"(iterations) : "r"(16) : "$8", "memory"
	);
	printf("A branch and its delay slot alone: %d\n", iterations);
}

// The likely form of the same loop, whose delay slot is nullified on the last
// pass and so counts one fewer.
static void testTwoInstructionLikely() {
	u32 iterations;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"li $8, 0\n"
		"1:\n"
		"bnel $8, %1, 1b\n"
		"addiu $8, $8, 1\n"
		"move %0, $8\n"
		".set reorder\n"
		: "=&r"(iterations) : "r"(16) : "$8", "memory"
	);
	printf("The likely form of the same loop: %d\n", iterations);
}

// A loop whose body loads what the branch compares, which is the hazard the
// padding is really there for.
static void testLoadInTheLoop() {
	static volatile u32 values[8] = {1, 1, 1, 1, 1, 1, 1, 0};
	u32 index;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"move $8, %1\n"
		"li $9, 0\n"
		"1:\n"
		"lw $10, 0($8)\n"
		"addiu $8, $8, 4\n"
		"bnez $10, 1b\n"
		"addiu $9, $9, 1\n"
		"move %0, $9\n"
		".set reorder\n"
		: "=&r"(index) : "r"(values) : "$8", "$9", "$10", "memory"
	);
	printf("A loop that loads its own condition: stopped after %d\n", index);
}

// The same loop with the load moved into the delay slot, so the branch reads a
// register the load has not written yet.
static void testLoadInTheDelaySlot() {
	static volatile u32 values[8] = {1, 1, 1, 0, 1, 1, 1, 1};
	u32 index, last;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"move $8, %2\n"
		"li $9, 0\n"
		"lw $10, 0($8)\n"
		"1:\n"
		"addiu $8, $8, 4\n"
		"addiu $9, $9, 1\n"
		"bnez $10, 1b\n"
		"lw $10, 0($8)\n"
		"move %0, $9\n"
		"move %1, $10\n"
		".set reorder\n"
		: "=&r"(index), "=&r"(last) : "r"(values) : "$8", "$9", "$10", "memory"
	);
	printf("A loop that loads in its delay slot: stopped after %d, last %d\n",
	       index, last);
}

// A backward branch to an address in the same instruction pair as the branch,
// which is where the fetch pairing matters.
static void testAlignedAndUnaligned() {
	for (int pad = 0; pad < 2; ++pad) {
		u32 iterations = 0;
		if (pad == 0) {
			asm volatile (
				".set noreorder\n"
				".set nomacro\n"
				".align 3\n"
				"li $8, 0\n"
				"1:\n"
				"addiu $8, $8, 1\n"
				"sltiu $9, $8, 10\n"
				"bnez $9, 1b\n"
				"nop\n"
				"move %0, $8\n"
				".set reorder\n"
				: "=&r"(iterations) : : "$8", "$9", "memory"
			);
		} else {
			asm volatile (
				".set noreorder\n"
				".set nomacro\n"
				".align 3\n"
				"nop\n"
				"li $8, 0\n"
				"1:\n"
				"addiu $8, $8, 1\n"
				"sltiu $9, $8, 10\n"
				"bnez $9, 1b\n"
				"nop\n"
				"move %0, $8\n"
				".set reorder\n"
				: "=&r"(iterations) : : "$8", "$9", "memory"
			);
		}
		printf("A loop at an %s pair: %d\n", pad == 0 ? "even" : "odd ",
		       iterations);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testBodyLengths();
	testTwoInstructionLoop();
	testTwoInstructionLikely();
	testLoadInTheLoop();
	testLoadInTheDelaySlot();
	testAlignedAndUnaligned();

	printf("-- TEST END\n");
	return 0;
}
