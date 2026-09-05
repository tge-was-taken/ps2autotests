#include <common-ee.h>

// What the branch instructions do beyond taking or not taking: the link
// register they write whether or not they branch, a delay slot that changes
// the register the branch already read, and the nullification the likely forms
// perform.

// A branch that links writes the return address even on the path it does not
// take, which is what makes BLTZAL a way to read the program counter.
static void testLinkOnNotTaken() {
	u32 taken, notTaken;

	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"move $2, $0\n"
		"bltzal %2, 1f\n"
		"nop\n"
		"1:\n"
		"move %0, $31\n"
		".set reorder\n"
		: "=r"(notTaken) : "0"(0), "r"(1) : "$2", "$31", "memory"
	);
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"bltzal %2, 1f\n"
		"nop\n"
		"1:\n"
		"move %0, $31\n"
		".set reorder\n"
		: "=r"(taken) : "0"(0), "r"(-1) : "$31", "memory"
	);
	printf("bltzal writes the link register: taken %s, not taken %s\n",
	       taken != 0 ? "yes" : "no", notTaken != 0 ? "yes" : "no");
	printf("  the two addresses differ by %d\n", (int)(taken - notTaken));
}

// The link register as the branch's own source, which the manual calls out
// because the write happens whatever the comparison decided.
static void testLinkAsSource() {
	u32 result;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"li $31, -1\n"
		// The assembler refuses this source register, so it is encoded by hand
		// with a target two instructions ahead.
		".word 0x07F00001\n"
		"nop\n"
		"move %0, $31\n"
		".set reorder\n"
		: "=r"(result) : : "$31", "memory"
	);
	printf("bltzal on the link register itself: still an address %s\n",
	       result > 0x100000 ? "yes" : "no");
}

// JALR writing the register it jumped through.
static void testJalrSameRegister() {
	u32 target, after;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"la $8, 1f\n"
		"move %0, $8\n"
		// The assembler refuses one register for both operands.
		".word 0x01004009\n"
		"nop\n"
		"1:\n"
		"move %1, $8\n"
		".set reorder\n"
		: "=&r"(target), "=&r"(after) : : "$8", "$31", "memory"
	);
	printf("jalr through the register it links to: link is %d past the target\n",
	       (int)(after - target));
}

// A delay slot that overwrites the register the branch compared, proving the
// comparison happened first.
static void testDelaySlotOverwritesSource() {
	u32 branched, value;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"li $8, 1\n"
		"li %0, 0\n"
		"bnez $8, 1f\n"
		"li $8, 0\n"
		"li %0, 2\n"
		"1:\n"
		"move %1, $8\n"
		".set reorder\n"
		: "=&r"(branched), "=&r"(value) : : "$8", "memory"
	);
	printf("A delay slot that clears the compared register: branched %s, "
	       "register %d\n", branched == 0 ? "yes" : "no", value);
}

// A delay slot that overwrites the register a jump takes its target from.
static void testDelaySlotOverwritesTarget() {
	u32 arrived;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"la $8, 1f\n"
		"li %0, 0\n"
		"jr $8\n"
		"li $8, 0\n"
		"li %0, 2\n"
		"1:\n"
		".set reorder\n"
		: "=&r"(arrived) : : "$8", "memory"
	);
	printf("A delay slot that clears the jump register: arrived %s\n",
	       arrived == 0 ? "yes" : "no");
}

// The likely forms, whose delay slot only runs when the branch is taken.
#define LIKELY_CASE(NAME, OP, SETUP) \
	do { \
		u32 slotRan, arrived; \
		asm volatile ( \
			".set noreorder\n" \
			".set nomacro\n" \
			"li %0, 0\n" \
			"li %1, 0\n" \
			SETUP \
			OP " $8, 1f\n" \
			"li %0, 1\n" \
			"li %1, 2\n" \
			"1:\n" \
			".set reorder\n" \
			: "=&r"(slotRan), "=&r"(arrived) : : "$8", "memory" \
		); \
		printf("  %-24s slot ran %d, fell through %d\n", NAME, slotRan, arrived); \
	} while (0)

static void testLikelyNullification() {
	printf("The likely forms:\n");
	LIKELY_CASE("beqzl taken",     "beqzl",  "li $8, 0\n");
	LIKELY_CASE("beqzl not taken", "beqzl",  "li $8, 1\n");
	LIKELY_CASE("bnezl taken",     "bnezl",  "li $8, 1\n");
	LIKELY_CASE("bnezl not taken", "bnezl",  "li $8, 0\n");
	LIKELY_CASE("bgezl taken",     "bgezl",  "li $8, 0\n");
	LIKELY_CASE("bgezl not taken", "bgezl",  "li $8, -1\n");
	LIKELY_CASE("bgtzl taken",     "bgtzl",  "li $8, 1\n");
	LIKELY_CASE("bgtzl not taken", "bgtzl",  "li $8, 0\n");
	LIKELY_CASE("blezl taken",     "blezl",  "li $8, 0\n");
	LIKELY_CASE("blezl not taken", "blezl",  "li $8, 1\n");
	LIKELY_CASE("bltzl taken",     "bltzl",  "li $8, -1\n");
	LIKELY_CASE("bltzl not taken", "bltzl",  "li $8, 0\n");
}

// The likely-and-linking forms, where the link register is written even on the
// path whose delay slot is nullified.
static void testLikelyLink() {
	printf("The likely forms that link:\n");

	u32 slotRan, link;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"li %0, 0\n"
		"move $31, $0\n"
		"li $8, 0\n"
		"bltzall $8, 1f\n"
		"li %0, 1\n"
		"1:\n"
		"move %1, $31\n"
		".set reorder\n"
		: "=&r"(slotRan), "=&r"(link) : : "$8", "$31", "memory"
	);
	printf("  bltzall not taken: slot ran %d, link written %s\n", slotRan,
	       link != 0 ? "yes" : "no");

	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"li %0, 0\n"
		"move $31, $0\n"
		"li $8, -1\n"
		"bltzall $8, 1f\n"
		"li %0, 1\n"
		"1:\n"
		"move %1, $31\n"
		".set reorder\n"
		: "=&r"(slotRan), "=&r"(link) : : "$8", "$31", "memory"
	);
	printf("  bltzall taken: slot ran %d, link written %s\n", slotRan,
	       link != 0 ? "yes" : "no");
}

// A branch whose target is the delay slot of the branch before it.
static void testBranchIntoDelaySlot() {
	u32 count;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"li %0, 0\n"
		"li $9, 0\n"
		"2:\n"
		"addiu %0, %0, 1\n"
		"slti $8, %0, 3\n"
		"beqz $8, 3f\n"
		"addiu $9, $9, 16\n"
		"b 2b\n"
		"nop\n"
		"3:\n"
		"or %0, %0, $9\n"
		".set reorder\n"
		: "=&r"(count) : : "$8", "$9", "memory"
	);
	printf("A loop whose exit branch has a working delay slot: %08x\n", count);
}

// The delay slot of a jump that is itself the last instruction before the
// target, so the same word is both the slot and reachable by falling through.
static void testSharedDelaySlot() {
	u32 value;
	asm volatile (
		".set noreorder\n"
		".set nomacro\n"
		"li %0, 0\n"
		"la $8, 1f\n"
		"jr $8\n"
		"1:\n"
		"addiu %0, %0, 1\n"
		"addiu %0, %0, 16\n"
		".set reorder\n"
		: "=&r"(value) : : "$8", "memory"
	);
	printf("A jump whose target is its own delay slot: %d\n", value);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testLinkOnNotTaken();
	testLinkAsSource();
	testJalrSameRegister();
	testDelaySlotOverwritesSource();
	testDelaySlotOverwritesTarget();
	testLikelyNullification();
	testLikelyLink();
	testBranchIntoDelaySlot();
	testSharedDelaySlot();

	printf("-- TEST END\n");
	return 0;
}
