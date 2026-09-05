#include "values.h"

const NamedValue edgeValues[] = {
	{"+0",     0x00000000},
	{"-0",     0x80000000},
	{"+1",     0x3F800000},
	{"-1",     0xBF800000},
	{"+2",     0x40000000},
	{"2-eps",  0x3FFFFFFF},
	{"eps",    0x33800000},
	{"dnmin",  0x00000001},
	{"dnmax",  0x007FFFFF},
	{"nrmin",  0x00800000},
	{"max",    0x7F7FFFFF},
	{"-max",   0xFF7FFFFF},
	{"inf",    0x7F800000},
	{"-inf",   0xFF800000},
	{"nan",    0x7FFFFFFF},
	{"-nan",   0xFFFFFFFF},
};

const int edgeValueCount = sizeof(edgeValues) / sizeof(edgeValues[0]);

const u32 *constantAt(int vu, int index) {
	u8 *base = vu == 0 ? vu0_mem : vu1_mem;
	return (const u32 *)(base + constantBase + 16 * index);
}

void loadConstant(int vu, int index, u32 bits) {
	u32 *dest = (u32 *)constantAt(vu, index);
	dest[0] = bits;
	dest[1] = bits;
	dest[2] = bits;
	dest[3] = bits;
}

void loadConstants(int vu, const NamedValue *values, int count) {
	for (int i = 0; i < count; ++i) {
		loadConstant(vu, i, values[i].bits);
	}
}
