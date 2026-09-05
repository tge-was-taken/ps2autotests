#include "shared.h"
#include <string.h>

static float CF_PATTERN[3][4] = {
	{(float)0x45678123, (float)0x9ABCDEF0, (float)0xDEADBEEF, (float)0xC0DE1337},
	{(float)0x23456789, (float)0xABCDEF01, (float)0xBEEFDEAD, (float)0xC0DEC0DE},
	{(float)0x8899AABB, (float)0xCCDDEEFF, (float)0x00112233, (float)0x44556677},
};

#define LOADF_OP_FUNC(OP) \
template <int offset> \
inline void LOADF_##OP(register float &res, void *address) { \
	asm volatile ( \
		#OP " %0, %2(%1)\n" \
		"sync\n" \
		: "+&f"(res) : "r"(address), "i"(offset) \
	); \
}

#define STOREF_OP_FUNC(OP) \
template <int offset> \
inline void STOREF_##OP(const register float &val, void *address) { \
	asm volatile ( \
		#OP " %0, %2(%1)\n" \
		"sync\n" \
		: : "f"(val), "r"(address), "i"(offset) \
	); \
}

static inline void PRINT_MF(const void *m, bool newline) {
	const FloatBits result = *(FloatBits *)m;

	printf("%08x/", result.u);
	printFloatString(result);
	if (newline) {
		printf("\n");
	}
}

#define LOADF_OP_DO_I(OP, t, s) \
	SET_U32<t>(ft); \
	LOADF_##OP<s>(ft, CF_PATTERN[1]); \
	printf("  %s %+d: ", #OP, (u32)s); PRINT_F(ft, true);

#define LOADF_OP_DO_M(OP, t, s) \
	SET_M(ft, t); \
	LOADF_##OP<s>(ft, CF_PATTERN[1]); \
	printf("  %s +%d: ", #OP, (u32)s); PRINT_F(ft, true);

#define TEST_LOADF_FUNC(OP) \
LOADF_OP_FUNC(OP); \
static void test_##OP() { \
	register float ft; \
	SET_M(ft, CF_GARBAGE1); \
	 \
	printf("%s:\n", #OP); \
	 \
	LOADF_OP_DO_I(OP, 0x3F800001, 0); \
	LOADF_OP_DO_I(OP, 0x3F800001, 4); \
	LOADF_OP_DO_I(OP, 0x3F800001, -4); \
	 \
	LOADF_OP_DO_M(OP, CF_GARBAGE1, 0); \
	LOADF_OP_DO_M(OP, CF_GARBAGE1, 4); \
	LOADF_OP_DO_M(OP, CF_GARBAGE1, -4); \
	\
	printf("\n"); \
}

#define STOREF_OP_DO_I(OP, t, s) \
	memcpy(buffer, CF_PATTERN, sizeof(buffer)); \
	SET_U32<t>(ft); \
	STOREF_##OP<s>(ft, &buffer[1]); \
	printf("  %s %+d: ", #OP, (u32)s); PRINT_MF(&buffer[1 + (s) / 4], true);

#define STOREF_OP_DO_M(OP, t, s) \
	memcpy(buffer, CF_PATTERN, sizeof(buffer)); \
	SET_M(ft, t); \
	STOREF_##OP<s>(ft, &buffer[1]); \
	printf("  %s +%d: ", #OP, (u32)s); PRINT_MF(&buffer[1 + (s) / 4], true);

#define TEST_STOREF_FUNC(OP) \
STOREF_OP_FUNC(OP); \
static void test_##OP() { \
	float buffer[3]; \
	register float ft; \
	SET_M(ft, CF_GARBAGE1); \
	 \
	printf("%s:\n", #OP); \
	 \
	STOREF_OP_DO_I(OP, 0x3F800001, 0); \
	STOREF_OP_DO_I(OP, 0x3F800001, 4); \
	STOREF_OP_DO_I(OP, 0x3F800001, -4); \
	 \
	STOREF_OP_DO_M(OP, CF_GARBAGE1, 0); \
	STOREF_OP_DO_M(OP, CF_GARBAGE1, 4); \
	STOREF_OP_DO_M(OP, CF_GARBAGE1, -4); \
	\
	printf("\n"); \
}

static void test_cfc1() {
	// Seems like 64-bit constants (ULL) compose incorrectly.
	// Compiler bug?  Oh well, let's build it manually.
	register u128 fcr31 = 0x12345678;
	asm volatile (
		"dsll32 $14, %0, 0\n"
		"or $14, $14, %0\n"
		"pcpyld %0, $14, $14\n"
		"cfc1 %0, $31\n"
		"sync\n"
		: "+&r"(fcr31) : : "$14"
	);

	printf("cfc1: ");
	PRINT_R(fcr31, true);

	asm volatile (
		"cfc1 $0, $31\n"
		"sync\n"
		"por %0, $0, $0\n"
		: "+&r"(fcr31) : : "$14"
	);

	printf("cfc1 -> $0: ");
	PRINT_R(fcr31, true);
}

static void test_ctc1() {
	// Seems like 64-bit constants (ULL) compose incorrectly.
	// Compiler bug?  Oh well, let's build it manually.
	register u128 fcr31 = 0xFFFFFFFF;
	asm volatile (
		"dsll32 $14, %0, 0\n"
		"or $14, $14, %0\n"
		"pcpyld %0, $14, $14\n"
		"ctc1 %0, $31\n"
		"sync\n"
		"cfc1 %0, $31\n"
		"sync\n"
		: "+&r"(fcr31) : : "$14"
	);

	printf("ctc1: ");
	PRINT_R(fcr31, true);
}

static void test_mfc1() {
	register float f = -1.0f;
	register u128 r = 0x0000000080F0000FULL;
	asm volatile (
		"dsll32 $14, %0, 0\n"
		"or $14, $14, %0\n"
		"pcpyld %0, $14, $14\n"
		"mfc1 %0, %1\n"
		"sync\n"
		: "+&r"(r) : "f"(f) : "$14"
	);

	printf("mfc1 negative: ");
	PRINT_R(r, true);

	f = 1.0f;
	r = 0x0000000000F0000FULL;
	asm volatile (
		"dsll32 $14, %0, 0\n"
		"or $14, $14, %0\n"
		"pcpyld %0, $14, $14\n"
		"mfc1 %0, %1\n"
		"sync\n"
		: "+&r"(r) : "f"(f) : "$14"
	);

	printf("mfc1 positive: ");
	PRINT_R(r, true);

	asm volatile (
		"mfc1 $0, %1\n"
		"sync\n"
		"por %0, $0, $0\n"
		: "+&r"(r) : "f"(f) : "$14"
	);

	printf("mfc1 -> $0: ");
	PRINT_R(r, true);
}

static void test_mtc1() {
	register float f = 1.0f;
	register u128 r = 0x0000000080F0000FULL;
	asm volatile (
		"dsll32 $14, %1, 0\n"
		"or $14, $14, %1\n"
		"pcpyld %1, $14, $14\n"
		"mtc1 %1, %0\n"
		"sync\n"
		: "+&f"(f) : "r"(r) : "$14"
	);

	printf("mtc1 negative: ");
	PRINT_F(f, true);

	f = 1.0f;
	r = 0x00F0000F;

	asm volatile (
		"lui $14, 0xFFFF\n"
		"ori $14, $14, 0xFFFF\n"
		"dsll32 $14, $14, 0\n"
		"or $14, $14, %1\n"
		"pcpyld %1, $14, $14\n"
		"mtc1 %1, %0\n"
		"sync\n"
		: "+&f"(f) : "r"(r) : "$14"
	);

	printf("mtc1 positive: ");
	PRINT_F(f, true);
}

TEST_LOADF_FUNC(lwc1)
TEST_STOREF_FUNC(swc1)

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	test_lwc1();
	test_swc1();

	test_cfc1();
	test_ctc1();

	test_mfc1();
	test_mtc1();

	printf("-- TEST END\n");

	return 0;
}
