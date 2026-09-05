#include <common-ee.h>
#include <string.h>
#include "../test_runner.h"
#include "../values.h"

// The elementary function unit exists on VU1 and not on VU0, so what VU0 does
// with an instruction that names it is unrecorded rather than documented.  Each
// op is run on both units with the same input, so the two columns say what the
// missing unit costs.

static const u32 input[4] = {0x40490FDB, 0x3F800000, 0x40000000, 0x40400000};

// The runner loads a register from vu memory rather than from here, so the
// four lanes go in as four constants and are written one lane at a time.
static void loadInput(TestRunner &runner, int vu, VU::Reg r) {
	for (int lane = 0; lane < 4; ++lane) {
		loadConstant(vu, lane, input[lane]);
	}
	for (int lane = 0; lane < 4; ++lane) {
		runner.WrLoadFloatRegister(VU::Dest(VU::DEST_X >> lane), r,
		                           constantAt(vu, lane));
	}
}

struct Case {
	const char *name;
	VU::LowerOp (*build)();
};

static VU::LowerOp buildEsadd()  { return VU::ESADD(VU::VF01); }
static VU::LowerOp buildErsadd() { return VU::ERSADD(VU::VF01); }
static VU::LowerOp buildEleng()  { return VU::ELENG(VU::VF01); }
static VU::LowerOp buildErleng() { return VU::ERLENG(VU::VF01); }
static VU::LowerOp buildEsum()   { return VU::ESUM(VU::VF01); }
static VU::LowerOp buildEsqrt()  { return VU::ESQRT(VU::FIELD_X, VU::VF01); }
static VU::LowerOp buildErsqrt() { return VU::ERSQRT(VU::FIELD_X, VU::VF01); }
static VU::LowerOp buildEsin()   { return VU::ESIN(VU::FIELD_X, VU::VF01); }
static VU::LowerOp buildEexp()   { return VU::EEXP(VU::FIELD_X, VU::VF01); }
static VU::LowerOp buildEatan()  { return VU::EATAN(VU::FIELD_X, VU::VF01); }
static VU::LowerOp buildEatanxy(){ return VU::EATANxy(VU::VF01); }

static const Case cases[] = {
	{"ESADD",   &buildEsadd},
	{"ERSADD",  &buildErsadd},
	{"ELENG",   &buildEleng},
	{"ERLENG",  &buildErleng},
	{"ESUM",    &buildEsum},
	{"ESQRT",   &buildEsqrt},
	{"ERSQRT",  &buildErsqrt},
	{"ESIN",    &buildEsin},
	{"EEXP",    &buildEexp},
	{"EATAN",   &buildEatan},
	{"EATANxy", &buildEatanxy},
};
static const int caseCount = sizeof(cases) / sizeof(cases[0]);

// The result register read straight after the op, with no wait, on each unit.
static void testEachOp(int vu) {
	printf("On VU%d, P after each op and eight idle steps:\n", vu);
	for (int i = 0; i < caseCount; ++i) {
		TestRunner runner(vu);
		loadInput(runner, vu, VU::VF01);
		runner.Wr(cases[i].build());
		for (int step = 0; step < 12; ++step) {
			runner.Wr(VU::NOP());
		}
		runner.Wr(VU::MFP(VU::DEST_X, VU::VF02));
		runner.Wr(VU::NOP());
		printf("  %-8s ", cases[i].name);
		runner.PrintRegisterFieldHex(VU::VF02, VU::FIELD_X, true);
		runner.Execute();
	}
}

// WAITP, which is how a program on VU1 knows the result is ready.  A unit with
// no such unit has nothing to wait for.
static void testWait(int vu) {
	printf("On VU%d, with WAITP instead of idle steps:\n", vu);
	for (int i = 0; i < caseCount; ++i) {
		TestRunner runner(vu);
		loadInput(runner, vu, VU::VF01);
		runner.Wr(cases[i].build());
		runner.Wr(VU::WAITP());
		runner.Wr(VU::MFP(VU::DEST_X, VU::VF02));
		runner.Wr(VU::NOP());
		printf("  %-8s ", cases[i].name);
		runner.PrintRegisterFieldHex(VU::VF02, VU::FIELD_X, true);
		runner.Execute();
	}
}

// The register the unit writes, read before anything wrote it.
static void testUntouched(int vu) {
	printf("On VU%d, P read with nothing having written it:\n", vu);
	TestRunner runner(vu);
	runner.Wr(VU::MFP(VU::DEST_XYZW, VU::VF02));
	runner.Wr(VU::NOP());
	printf("  ");
	runner.PrintRegisterHex(VU::VF02, true);
	runner.Execute();
}

// How many steps the result takes to appear, which is the number a program
// has to leave between the op and the read.
static void testLatency(int vu) {
	static const int op = 2;

	printf("On VU%d, ELENG read after each number of steps:\n", vu);
	for (int steps = 0; steps <= 12; ++steps) {
		TestRunner runner(vu);
		loadInput(runner, vu, VU::VF01);
		runner.Wr(cases[op].build());
		for (int i = 0; i < steps; ++i) {
			runner.Wr(VU::NOP());
		}
		runner.Wr(VU::MFP(VU::DEST_X, VU::VF02));
		runner.Wr(VU::NOP());
		printf("  %2d steps ", steps);
		runner.PrintRegisterFieldHex(VU::VF02, VU::FIELD_X, true);
		runner.Execute();
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testUntouched(0);
	testUntouched(1);
	testEachOp(1);
	testEachOp(0);
	testWait(1);
	testWait(0);
	testLatency(1);
	testLatency(0);

	printf("-- TEST END\n");
	return 0;
}
