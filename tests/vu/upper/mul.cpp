#include <common-ee.h>
#include "fmac_runner.h"

// The multiplier is where the vu departs from ieee the furthest, so these are
// products whose exact result needs more than 24 mantissa bits and whose
// rounding therefore shows which bits the hardware keeps.
static const NamedValue roundingValues[] = {
	{"1+1u",  0x3F800001},
	{"1+2u",  0x3F800002},
	{"1+3u",  0x3F800003},
	{"1.5",   0x3FC00000},
	{"1.5+u", 0x3FC00001},
	{"3-u",   0x403FFFFF},
	{"2^12",  0x45800000},
	{"2^12+", 0x45800001},
	{"2^-12", 0x39800000},
	{"m24",   0x4B7FFFFF},
	{"m24+1", 0x4B800000},
	{"sqrt2", 0x3FB504F3},
	{"pi",    0x40490FDB},
	{"1/3",   0x3EAAAAAB},
	{"1/7",   0x3E124925},
	{"7",     0x40E00000},
};

static const int roundingCount = sizeof(roundingValues) / sizeof(roundingValues[0]);

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	FmacRunner runner(0);

	runner.PerformPairs("MUL", &VU::MUL);
	runner.PerformBroadcast("MULbc x", &VU::MULbc, VU::FIELD_X);
	runner.PerformBroadcastLanes("MULbc", &VU::MULbc);
	runner.PerformImmediate("MULi", &VU::MULi);
	runner.PerformQuotient("MULq", &VU::MULq);

	runner.PerformAccumulate("MULA", &VU::MULA);
	runner.PerformAccumulateBroadcast("MULAbc x", &VU::MULAbc, VU::FIELD_X);
	runner.PerformAccumulateImmediate("MULAi", &VU::MULAi);
	runner.PerformAccumulateQuotient("MULAq", &VU::MULAq);

	runner.PerformDestMasks("MUL", &VU::MUL, 2, 4);
	runner.PerformLanes("MUL", &VU::MUL);

	runner.UseValues(roundingValues, roundingCount);
	runner.PerformPairs("MUL rounding", &VU::MUL);

	printf("-- TEST END\n");
	return 0;
}
