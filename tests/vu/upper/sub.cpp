#include <common-ee.h>
#include "fmac_runner.h"

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	FmacRunner runner(0);

	runner.PerformPairs("SUB", &VU::SUB);
	runner.PerformBroadcast("SUBbc x", &VU::SUBbc, VU::FIELD_X);
	runner.PerformBroadcastLanes("SUBbc", &VU::SUBbc);
	runner.PerformImmediate("SUBi", &VU::SUBi);
	runner.PerformQuotient("SUBq", &VU::SUBq);

	runner.PerformAccumulate("SUBA", &VU::SUBA);
	runner.PerformAccumulateBroadcast("SUBAbc x", &VU::SUBAbc, VU::FIELD_X);
	runner.PerformAccumulateImmediate("SUBAi", &VU::SUBAi);
	runner.PerformAccumulateQuotient("SUBAq", &VU::SUBAq);

	runner.PerformDestMasks("SUB", &VU::SUB, 2, 4);
	runner.PerformDestMasks("SUB", &VU::SUB, 10, 11);
	runner.PerformLanes("SUB", &VU::SUB);

	printf("-- TEST END\n");
	return 0;
}
