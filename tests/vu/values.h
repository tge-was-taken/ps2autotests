#ifndef PS2AUTOTESTS_VU_VALUES_H
#define PS2AUTOTESTS_VU_VALUES_H

#include <common-ee.h>
#include "assemble.h"

struct NamedValue {
	const char *name;
	u32 bits;
};

// test_runner.cpp fills 0x100 through 0x430 with 0xCC before every run, so a
// constant that has to survive Execute() lives above that.
static const u32 constantBase = 0x440;

// What a destination register holds going in, so a masked off lane is obvious.
static const int junkIndex = 40;
static const u32 junkBits = 0x0BADF00D;

// Writes one value into vu memory, splatted across xyzw.
void loadConstant(int vu, int index, u32 bits);
void loadConstants(int vu, const NamedValue *values, int count);

// Where loadConstant put an index.
const u32 *constantAt(int vu, int index);

// The bit patterns worth crossing against each other: signed zeroes, the
// smallest and largest of each class, and the patterns ieee would call
// infinity and nan.
extern const NamedValue edgeValues[];
extern const int edgeValueCount;

#endif
