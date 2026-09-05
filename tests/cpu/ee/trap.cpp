#include <common-ee.h>

// The trapping instructions.  A trap that fires ends the program, so what is
// recorded is the comparison each one is built on, then the conditions it
// survives, and last the conditions it does not.  A capture that stops inside
// the final section names the instruction that fired on its last line.

struct Pair {
	const char *name;
	s64 a;
	s64 b;
};

static const Pair pairs[] = {
	{"zero and zero          ", 0, 0},
	{"one and one            ", 1, 1},
	{"minus one and minus one", -1, -1},
	{"minus one and one      ", -1, 1},
	{"one and minus one      ", 1, -1},
	{"32-bit high and low    ", 0x7FFFFFFF, (s64)(s32)0x80000000},
	{"same low half          ", 0x0000000100000001ll, 0x0000000200000001ll},
	{"sign in the high half  ", -0x7FFFFFFFFFFFFFFFll - 1, 1},
	{"largest and smallest   ", 0x7FFFFFFFFFFFFFFFll, -0x7FFFFFFFFFFFFFFFll - 1},
	{"unsigned wrap          ", -1, 0x7FFFFFFFFFFFFFFFll},
};
static const int pairCount = sizeof(pairs) / sizeof(pairs[0]);

// The comparison the trap instructions are built on, taken with instructions
// that cannot fire, so the whole table is recorded whatever the traps do.
static void testComparisons() {
	printf("The comparison behind each trap:\n");
	for (int i = 0; i < pairCount; ++i) {
		u32 less, lessUnsigned, equal;
		asm volatile (
			"slt %0, %3, %4\n"
			"sltu %1, %3, %4\n"
			"xor %2, %3, %4\n"
			"sltiu %2, %2, 1\n"
			: "=&r"(less), "=&r"(lessUnsigned), "=&r"(equal)
			: "r"(pairs[i].a), "r"(pairs[i].b)
		);
		printf("  %s %016llx %016llx: lt %d ltu %d eq %d\n", pairs[i].name,
		       pairs[i].a, pairs[i].b, less, lessUnsigned, equal);
	}
}

// Each instruction on a pair its condition is false for.
static void testConditionsThatSurvive() {
	printf("Conditions that do not fire:\n");

	asm volatile ("teq %0, %1\n" : : "r"(1), "r"(2));
	printf("  teq on one and two: survived\n");
	asm volatile ("tne %0, %1\n" : : "r"(3), "r"(3));
	printf("  tne on three and three: survived\n");
	asm volatile ("tge %0, %1\n" : : "r"(-1), "r"(1));
	printf("  tge on minus one and one: survived\n");
	asm volatile ("tgeu %0, %1\n" : : "r"(1), "r"(-1));
	printf("  tgeu on one and minus one: survived\n");
	asm volatile ("tlt %0, %1\n" : : "r"(1), "r"(-1));
	printf("  tlt on one and minus one: survived\n");
	asm volatile ("tltu %0, %1\n" : : "r"(-1), "r"(1));
	printf("  tltu on minus one and one: survived\n");

	asm volatile ("teqi %0, 0x1234\n" : : "r"(0x1235));
	printf("  teqi against 1234: survived\n");
	asm volatile ("tnei %0, 0x1234\n" : : "r"(0x1234));
	printf("  tnei against 1234: survived\n");
	asm volatile ("tgei %0, -1\n" : : "r"(-2));
	printf("  tgei against minus one: survived\n");
	asm volatile ("tlti %0, -1\n" : : "r"(0));
	printf("  tlti against minus one: survived\n");

	// The immediate is signed on both, so the unsigned forms compare a value
	// that has been widened before the comparison rather than after.
	asm volatile ("tgeiu %0, 1\n" : : "r"(-1));
	printf("  tgeiu against one, register minus one: survived\n");
	asm volatile ("tltiu %0, -1\n" : : "r"(1));
	printf("  tltiu against minus one, register one: survived\n");
}

// The largest operands the checked arithmetic accepts.
static void testOverflowBoundaries() {
	printf("Checked arithmetic at its boundary:\n");

	s32 sum32, difference32;
	asm volatile ("add %0, %1, %2\n" : "=r"(sum32) : "r"(0x7FFFFFFE), "r"(1));
	printf("  add 7ffffffe and one: %08x\n", sum32);
	asm volatile ("add %0, %1, %2\n" : "=r"(sum32) : "r"(0x80000000), "r"(-1 + 1));
	printf("  add 80000000 and zero: %08x\n", sum32);
	asm volatile ("sub %0, %1, %2\n" : "=r"(difference32) : "r"(0x80000001), "r"(1));
	printf("  sub 80000001 and one: %08x\n", difference32);
	asm volatile ("addi %0, %1, 1\n" : "=r"(sum32) : "r"(0x7FFFFFFE));
	printf("  addi 7ffffffe and one: %08x\n", sum32);

	// The unchecked forms take the same operands one step further.
	asm volatile ("addu %0, %1, %2\n" : "=r"(sum32) : "r"(0x7FFFFFFF), "r"(1));
	printf("  addu 7fffffff and one: %08x\n", sum32);
	asm volatile ("subu %0, %1, %2\n" : "=r"(difference32) : "r"(0x80000000), "r"(1));
	printf("  subu 80000000 and one: %08x\n", difference32);

	s64 sum64;
	asm volatile ("dadd %0, %1, %2\n" : "=r"(sum64)
	              : "r"(0x7FFFFFFFFFFFFFFEll), "r"(1ll));
	printf("  dadd 7ffffffffffffffe and one: %016llx\n", sum64);
	asm volatile ("daddu %0, %1, %2\n" : "=r"(sum64)
	              : "r"(0x7FFFFFFFFFFFFFFFll), "r"(1ll));
	printf("  daddu 7fffffffffffffff and one: %016llx\n", sum64);

	// A 32-bit overflow that is not a 64-bit one, which the wide forms accept.
	asm volatile ("dadd %0, %1, %2\n" : "=r"(sum64) : "r"(0x7FFFFFFFll), "r"(1ll));
	printf("  dadd 000000007fffffff and one: %016llx\n", sum64);
}

// A condition each instruction does fire on.  The line naming the instruction
// is printed first, so a capture that ends there says which one fired.
static void testConditionsThatFire() {
	printf("Conditions that fire:\n");

	printf("  teq on one and one\n");
	asm volatile ("teq %0, %1\n" : : "r"(1), "r"(1));
	printf("  teq returned\n");

	printf("  tne on one and two\n");
	asm volatile ("tne %0, %1\n" : : "r"(1), "r"(2));
	printf("  tne returned\n");

	printf("  tge on one and minus one\n");
	asm volatile ("tge %0, %1\n" : : "r"(1), "r"(-1));
	printf("  tge returned\n");

	printf("  add 7fffffff and one\n");
	{
		s32 sum;
		asm volatile ("add %0, %1, %2\n" : "=r"(sum) : "r"(0x7FFFFFFF), "r"(1));
		printf("  add returned %08x\n", sum);
	}

	printf("  dadd 7fffffffffffffff and one\n");
	{
		s64 sum;
		asm volatile ("dadd %0, %1, %2\n" : "=r"(sum)
		              : "r"(0x7FFFFFFFFFFFFFFFll), "r"(1ll));
		printf("  dadd returned %016llx\n", sum);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testComparisons();
	testConditionsThatSurvive();
	testOverflowBoundaries();
	testConditionsThatFire();

	printf("-- TEST END\n");
	return 0;
}
