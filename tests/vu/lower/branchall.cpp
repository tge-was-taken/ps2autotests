#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

// Every branch the unit has, taken and not taken, plus the cases around the
// delay slot: an operand written just before the branch, and a branch whose
// slot writes the register the branch read.

class BranchRunner : public TestRunner {
public:
	BranchRunner() : TestRunner(0) {
	}

	// VI01 says which path ran, so a branch that goes the wrong way is a
	// different number rather than a missing line.
	void PrintPath(const char *what) {
		printf("  %-28s taken %s, vi01 ", what, "");
		PrintRegister(VU::VI01, false);
		printf(" vi02 ");
		PrintRegister(VU::VI02, true);
	}

	void PerformConditional(const char *name, int s, int t) {
		using namespace VU;

		Reset();
		WrSetIntegerRegister(VI03, (u16)s);
		WrSetIntegerRegister(VI04, (u16)t);
		WrSetIntegerRegister(VI01, 0);
		WrSetIntegerRegister(VI02, 0);

		Label taken = IBEQ(VI03, VI04, NOP(), IADDIU(VI02, VI00, 1));
		Wr(IADDIU(VI01, VI00, 0x11));
		Label done = B(NOP(), NOP(), LowerOp());
		L(taken);
		Wr(IADDIU(VI01, VI00, 0x22));
		L(done);
		Execute();

		printf("  %-10s %5d %5d: vi01 ", name, s, t);
		PrintRegister(VI01, false);
		printf(" delay vi02 ");
		PrintRegister(VI02, true);
	}

#define BRANCH_ONE(NAME, EMIT) \
	void Perform_##NAME(int s) { \
		using namespace VU; \
		Reset(); \
		WrSetIntegerRegister(VI03, (u16)s); \
		WrSetIntegerRegister(VI01, 0); \
		WrSetIntegerRegister(VI02, 0); \
		Label taken = EMIT; \
		Wr(IADDIU(VI01, VI00, 0x11)); \
		Label done = B(NOP(), NOP(), LowerOp()); \
		L(taken); \
		Wr(IADDIU(VI01, VI00, 0x22)); \
		L(done); \
		Execute(); \
		printf("  %-10s %5d: vi01 ", #NAME, s); \
		PrintRegister(VI01, false); \
		printf(" delay vi02 "); \
		PrintRegister(VI02, true); \
	}

	BRANCH_ONE(IBGEZ, IBGEZ(VI03, NOP(), IADDIU(VI02, VI00, 1)))
	BRANCH_ONE(IBGTZ, IBGTZ(VI03, NOP(), IADDIU(VI02, VI00, 1)))
	BRANCH_ONE(IBLEZ, IBLEZ(VI03, NOP(), IADDIU(VI02, VI00, 1)))
	BRANCH_ONE(IBLTZ, IBLTZ(VI03, NOP(), IADDIU(VI02, VI00, 1)))

	void PerformNotEqual(int s, int t) {
		using namespace VU;

		Reset();
		WrSetIntegerRegister(VI03, (u16)s);
		WrSetIntegerRegister(VI04, (u16)t);
		WrSetIntegerRegister(VI01, 0);
		WrSetIntegerRegister(VI02, 0);

		Label taken = IBNE(VI03, VI04, NOP(), IADDIU(VI02, VI00, 1));
		Wr(IADDIU(VI01, VI00, 0x11));
		Label done = B(NOP(), NOP(), LowerOp());
		L(taken);
		Wr(IADDIU(VI01, VI00, 0x22));
		L(done);
		Execute();

		printf("  %-10s %5d %5d: vi01 ", "IBNE", s, t);
		PrintRegister(VI01, false);
		printf(" delay vi02 ");
		PrintRegister(VI02, true);
	}

	// An unconditional branch and the link register form.
	void PerformUnconditional() {
		using namespace VU;

		Reset();
		WrSetIntegerRegister(VI01, 0);
		WrSetIntegerRegister(VI02, 0);
		Label over = B(NOP(), NOP(), IADDIU(VI02, VI00, 1));
		Wr(IADDIU(VI01, VI00, 0x11));
		L(over);
		Wr(IADDIU(VI01, VI01, 0x22));
		Execute();

		printf("  %-10s: vi01 ", "B");
		PrintRegister(VI01, false);
		printf(" delay vi02 ");
		PrintRegister(VI02, true);

		Reset();
		WrSetIntegerRegister(VI01, 0);
		WrSetIntegerRegister(VI05, 0);
		Label call = BAL(NOP(), VI05, NOP(), IADDIU(VI02, VI00, 1));
		Wr(IADDIU(VI01, VI00, 0x11));
		L(call);
		Wr(IADDIU(VI01, VI01, 0x22));
		Execute();

		printf("  %-10s: vi01 ", "BAL");
		PrintRegister(VI01, false);
		printf(" link vi05 ");
		PrintRegister(VI05, true);
	}

	// The branch reads its operand before the slot runs, so a slot that writes
	// the same register cannot change the decision.
	void PerformSlotWritesOperand() {
		using namespace VU;

		printf("A delay slot that rewrites the compared register:\n");
		for (int initial = 0; initial < 2; ++initial) {
			Reset();
			WrSetIntegerRegister(VI03, (u16)initial);
			WrSetIntegerRegister(VI01, 0);

			Label taken = IBEQ(VI03, VI00, NOP(), IADDIU(VI03, VI00, 1 - initial));
			Wr(IADDIU(VI01, VI00, 0x11));
			Label done = B(NOP(), NOP(), LowerOp());
			L(taken);
			Wr(IADDIU(VI01, VI00, 0x22));
			L(done);
			Execute();

			printf("  vi03 starts %d: vi01 ", initial);
			PrintRegister(VI01, false);
			printf(" vi03 ends ");
			PrintRegister(VI03, true);
		}
	}

	// An operand written by the instruction just before the branch, which the
	// unit has to have forwarded.
	void PerformOperandJustBefore() {
		using namespace VU;

		printf("The compared register written right before the branch:\n");
		for (int gap = 0; gap < 3; ++gap) {
			Reset();
			WrSetIntegerRegister(VI01, 0);
			// A stale read would see the five and not branch; a forwarded one
			// sees the zero and does.
			WrSetIntegerRegister(VI03, 5);
			Wr(IADDIU(VI03, VI00, 0));
			for (int i = 0; i < gap; ++i) {
				Wr(NOP());
			}
			Label taken = IBEQ(VI03, VI00, NOP(), LowerOp());
			Wr(IADDIU(VI01, VI00, 0x11));
			Label done = B(NOP(), NOP(), LowerOp());
			L(taken);
			Wr(IADDIU(VI01, VI00, 0x22));
			L(done);
			Execute();

			printf("  gap %d: vi01 ", gap);
			PrintRegister(VI01, true);
		}
	}
};

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	BranchRunner runner;

	printf("IBEQ:\n");
	runner.PerformConditional("IBEQ", 0, 0);
	runner.PerformConditional("IBEQ", 1, 1);
	runner.PerformConditional("IBEQ", 0, 1);
	runner.PerformConditional("IBEQ", 1, 0);
	runner.PerformConditional("IBEQ", 0x7FFF, 0x7FFF);

	printf("IBNE:\n");
	runner.PerformNotEqual(0, 0);
	runner.PerformNotEqual(0, 1);
	runner.PerformNotEqual(0x7FFF, 0x7FFE);

	printf("Signed comparisons against zero:\n");
	static const int values[] = {0, 1, 2, 0x7FFF, 0x8000, 0xFFFF};
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		runner.Perform_IBGEZ(values[i]);
		runner.Perform_IBGTZ(values[i]);
		runner.Perform_IBLEZ(values[i]);
		runner.Perform_IBLTZ(values[i]);
	}

	printf("Unconditional:\n");
	runner.PerformUnconditional();

	runner.PerformSlotWritesOperand();
	runner.PerformOperandJustBefore();

	printf("-- TEST END\n");
	return 0;
}
