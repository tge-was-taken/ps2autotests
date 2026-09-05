#include <common-ee.h>
#include "fmac_runner.h"

// Index into edgeValues.
static const int one = 2;
static const int max = 10;

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	FmacRunner runner(0);

	runner.PerformPairsWithAccumulator("MADD", &VU::MADD, one);
	runner.PerformAccumulatorSweep("MADD", &VU::MADD, one, one);
	runner.PerformAccumulatorSweep("MADD", &VU::MADD, max, max);
	runner.PerformBroadcastWithAccumulator("MADDbc x", &VU::MADDbc, VU::FIELD_X, one);
	runner.PerformImmediateWithAccumulator("MADDi", &VU::MADDi, one);
	runner.PerformQuotientWithAccumulator("MADDq", &VU::MADDq, one);
	runner.PerformAccumulateWithAccumulator("MADDA", &VU::MADDA, one);

	runner.PerformPairsWithAccumulator("MSUB", &VU::MSUB, one);
	runner.PerformAccumulatorSweep("MSUB", &VU::MSUB, one, one);
	runner.PerformAccumulatorSweep("MSUB", &VU::MSUB, max, max);
	runner.PerformBroadcastWithAccumulator("MSUBbc x", &VU::MSUBbc, VU::FIELD_X, one);
	runner.PerformImmediateWithAccumulator("MSUBi", &VU::MSUBi, one);
	runner.PerformQuotientWithAccumulator("MSUBq", &VU::MSUBq, one);
	runner.PerformAccumulateWithAccumulator("MSUBA", &VU::MSUBA, one);

	printf("-- TEST END\n");
	return 0;
}
