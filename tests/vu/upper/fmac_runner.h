#ifndef PS2AUTOTESTS_VU_UPPER_FMAC_RUNNER_H
#define PS2AUTOTESTS_VU_UPPER_FMAC_RUNNER_H

#include <common-ee.h>
#include "../test_runner.h"
#include "../values.h"

class FmacRunner : public TestRunner {
public:
	FmacRunner(int vu);

	// Swaps the table the Perform methods cross.  The sample pairs index into
	// it, so a replacement needs at least as many entries as the edge values.
	void UseValues(const NamedValue *values, int count);

	typedef VU::UpperOp (*BinaryOp)(VU::Dest, VU::Reg, VU::Reg, VU::Reg, VU::Flags);
	typedef VU::UpperOp (*BroadcastOp)(VU::Dest, VU::Field, VU::Reg, VU::Reg, VU::Reg, VU::Flags);
	// ADDi, ADDq, ADDA and ABS all take (dest, two registers, flags), so one
	// pointer type covers them and the Perform that uses it says which it is.
	typedef VU::UpperOp (*RegisterOp)(VU::Dest, VU::Reg, VU::Reg, VU::Flags);
	// ADDAi, ADDAq: an accumulator destination and one register operand.
	typedef VU::UpperOp (*SingleOp)(VU::Dest, VU::Reg, VU::Flags);
	typedef VU::UpperOp (*BroadcastSingleOp)(VU::Dest, VU::Field, VU::Reg, VU::Reg, VU::Flags);

	// d = s op t for every pair of the edge values.  The arithmetic itself is
	// what earns the full cross; the operand sources below reuse a sample.
	void PerformPairs(const char *name, BinaryOp op);

	void PerformSample(const char *name, BinaryOp op);
	void PerformBroadcast(const char *name, BroadcastOp op, VU::Field bc);
	void PerformImmediate(const char *name, RegisterOp op);
	void PerformQuotient(const char *name, RegisterOp op);
	void PerformAccumulate(const char *name, RegisterOp op);
	void PerformAccumulateBroadcast(const char *name, BroadcastSingleOp op, VU::Field bc);
	void PerformAccumulateImmediate(const char *name, SingleOp op);
	void PerformAccumulateQuotient(const char *name, SingleOp op);

	// t = op(s), for abs and the converts.
	void PerformUnary(const char *name, RegisterOp op);

	// madd and msub read the accumulator, so it is set from a known value
	// first.  Setting it costs a subtract of zero, which flushes a denormal.
	void PerformPairsWithAccumulator(const char *name, BinaryOp op, int acc);
	void PerformSampleWithAccumulator(const char *name, BinaryOp op, int acc);
	void PerformBroadcastWithAccumulator(const char *name, BroadcastOp op, VU::Field bc, int acc);
	void PerformImmediateWithAccumulator(const char *name, RegisterOp op, int acc);
	void PerformQuotientWithAccumulator(const char *name, RegisterOp op, int acc);
	// The same pair against every accumulator value.
	void PerformAccumulatorSweep(const char *name, BinaryOp op, int s, int t);
	// The a forms, which read the accumulator and write it back.
	void PerformAccumulateWithAccumulator(const char *name, RegisterOp op, int acc);

	// Which lane a broadcast reads, with all four lanes holding something else.
	void PerformBroadcastLanes(const char *name, BroadcastOp op);
	// One pair through every destination mask, printing all four lanes.
	void PerformDestMasks(const char *name, BinaryOp op, int s, int t);
	// Four different pairs at once, so a per lane flag has somewhere to differ.
	void PerformLanes(const char *name, BinaryOp op);
	// What the divider leaves in Q for each edge value, since the q forms add
	// that rather than the value itself.
	void PerformQuotientValues();

protected:
	// Leaves Q holding the divider's answer for one edge value over 1.0.
	void WrSetQuotient(int index);
	// Leaves the accumulator holding one edge value less zero.
	void WrSetAccumulator(int index);
	// Snapshots the mac register into VI01, far enough after the op under test
	// for the flags to have arrived.
	void WrSnapshotMac();
	// Reads the accumulator without disturbing a signed zero: subtracting the
	// zero in VF00.x keeps -0 where adding it would give +0.
	void WrReadAccumulator(VU::Reg dest);

	void WrLoadOperands(int s, int t);

	const NamedValue *values_;
	int valueCount_;
	void PrintResult();
	void PrintAccumulated();
};

#endif
