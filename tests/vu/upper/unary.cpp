#include <common-ee.h>
#include "fmac_runner.h"

// ftoi reads a float and itof reads a signed integer, so the same 32 bits mean
// two different things.  These sit either side of each conversion's range.
static const NamedValue conversionValues[] = {
	{"+0",     0x00000000},
	{"-0",     0x80000000},
	{"+1",     0x3F800000},
	{"-1",     0xBF800000},
	{"+0.5",   0x3F000000},
	{"-0.5",   0xBF000000},
	{"1.5",    0x3FC00000},
	{"-1.5",   0xBFC00000},
	{"32767",  0x46FFFE00},
	{"32768",  0x47000000},
	{"2^23",   0x4B000000},
	{"2^31",   0x4F000000},
	{"-2^31",  0xCF000000},
	{"2^32",   0x4F800000},
	{"max",    0x7F7FFFFF},
	{"-max",   0xFF7FFFFF},
	{"i1",     0x00000001},
	{"i-1",    0xFFFFFFFF},
	{"i2^30",  0x40000000},
	{"i-2^30", 0xC0000000},
};

static const int conversionCount = sizeof(conversionValues) / sizeof(conversionValues[0]);

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	FmacRunner runner(0);

	runner.PerformUnary("ABS", &VU::ABS);

	runner.UseValues(conversionValues, conversionCount);
	runner.PerformUnary("ABS conversion values", &VU::ABS);
	runner.PerformUnary("FTOI0", &VU::FTOI0);
	runner.PerformUnary("FTOI4", &VU::FTOI4);
	runner.PerformUnary("FTOI12", &VU::FTOI12);
	runner.PerformUnary("FTOI15", &VU::FTOI15);
	runner.PerformUnary("ITOF0", &VU::ITOF0);
	runner.PerformUnary("ITOF4", &VU::ITOF4);
	runner.PerformUnary("ITOF12", &VU::ITOF12);
	runner.PerformUnary("ITOF15", &VU::ITOF15);

	printf("-- TEST END\n");
	return 0;
}
