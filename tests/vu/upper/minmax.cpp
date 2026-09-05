#include <common-ee.h>
#include "fmac_runner.h"

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	FmacRunner runner(0);

	runner.PerformPairs("MAX", &VU::MAX);
	runner.PerformBroadcast("MAXbc x", &VU::MAXbc, VU::FIELD_X);
	runner.PerformBroadcastLanes("MAXbc", &VU::MAXbc);
	runner.PerformImmediate("MAXi", &VU::MAXi);

	runner.PerformPairs("MINI", &VU::MINI);
	runner.PerformBroadcast("MINIbc x", &VU::MINIbc, VU::FIELD_X);
	runner.PerformBroadcastLanes("MINIbc", &VU::MINIbc);
	runner.PerformImmediate("MINIi", &VU::MINIi);

	runner.PerformDestMasks("MAX", &VU::MAX, 2, 4);
	runner.PerformDestMasks("MINI", &VU::MINI, 2, 4);
	runner.PerformLanes("MAX", &VU::MAX);
	runner.PerformLanes("MINI", &VU::MINI);

	printf("-- TEST END\n");
	return 0;
}
