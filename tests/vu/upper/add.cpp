#include <common-ee.h>
#include "fmac_runner.h"

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	FmacRunner runner(0);

	runner.PerformPairs("ADD", &VU::ADD);
	runner.PerformBroadcast("ADDbc x", &VU::ADDbc, VU::FIELD_X);
	runner.PerformBroadcastLanes("ADDbc", &VU::ADDbc);
	runner.PerformImmediate("ADDi", &VU::ADDi);
	runner.PerformQuotientValues();
	runner.PerformQuotient("ADDq", &VU::ADDq);

	runner.PerformAccumulate("ADDA", &VU::ADDA);
	runner.PerformAccumulateBroadcast("ADDAbc x", &VU::ADDAbc, VU::FIELD_X);
	runner.PerformAccumulateImmediate("ADDAi", &VU::ADDAi);
	runner.PerformAccumulateQuotient("ADDAq", &VU::ADDAq);

	runner.PerformDestMasks("ADD", &VU::ADD, 2, 4);
	runner.PerformDestMasks("ADD", &VU::ADD, 10, 10);
	runner.PerformLanes("ADD", &VU::ADD);

	printf("-- TEST END\n");
	return 0;
}
