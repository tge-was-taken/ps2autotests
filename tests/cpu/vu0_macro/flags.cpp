#include "macro.h"

// How long after an operation the three flag registers are readable from the
// ee, and what the interlocked form of the transfer changes.

// A gap of vnops between the operation and the read.
#define GAP_FUNC(N, GAP) \
static void runGap##N(Quad &out, const Quad &a, const Quad &b, Flags &flags) { \
	asm volatile ( \
		"lqc2 $vf1, 0(%3)\n" \
		"lqc2 $vf2, 0(%4)\n" \
		"lqc2 $vf3, 0(%5)\n" \
		"vmul.xyzw $vf3, $vf1, $vf2\n" \
		GAP \
		"cfc2 %0, $16\n" \
		"cfc2 %1, $17\n" \
		"cfc2 %2, $18\n" \
		"sqc2 $vf3, 0(%5)\n" \
		: "=&r"(flags.status), "=&r"(flags.mac), "=&r"(flags.clipping) \
		: "r"(&a), "r"(&b), "r"(&out) : "memory" \
	); \
}

GAP_FUNC(0, "")
GAP_FUNC(1, "vnop\n")
GAP_FUNC(2, "vnop\nvnop\n")
GAP_FUNC(3, "vnop\nvnop\nvnop\n")
GAP_FUNC(4, "vnop\nvnop\nvnop\nvnop\n")
GAP_FUNC(5, "vnop\nvnop\nvnop\nvnop\nvnop\n")
GAP_FUNC(6, "sync.p\n")
GAP_FUNC(7, "nop\nnop\nnop\nnop\n")

typedef void (*GapFunction)(Quad &, const Quad &, const Quad &, Flags &);

// A pattern that raises something in every lane, so a partial read is obvious.
static const u32 mixedLanes[4] = {0x00000000, 0xBF800000, 0x7F7FFFFF, 0x3F800000};

static void testDelay() {
	static const GapFunction runs[] = {
		&runGap0, &runGap1, &runGap2, &runGap3, &runGap4, &runGap5, &runGap6, &runGap7,
	};
	static const char *const names[] = {
		"no gap", "1 vnop", "2 vnop", "3 vnop", "4 vnop", "5 vnop", "sync.p", "4 nop",
	};

	printf("cfc2 after vmul:\n");
	for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
		Quad a, b, out;
		lanes(a, mixedLanes);
		splat(b, 0x7F7FFFFF);
		splat(out, junkBits);

		Flags flags;
		clearFlags();
		runs[i](out, a, b, flags);

		printf("  %-7s:", names[i]);
		printFlags(flags);
		printf("\n");
	}
}

// The transfer instructions have an interlocked form that waits for the unit.
static void testInterlocked() {
	Quad a, b, out;
	lanes(a, mixedLanes);
	splat(b, 0x7F7FFFFF);
	splat(out, junkBits);

	u32 plain = 0;
	u32 interlocked = 0;
	clearFlags();
	asm volatile (
		"lqc2 $vf1, 0(%2)\n"
		"lqc2 $vf2, 0(%3)\n"
		"vmul.xyzw $vf3, $vf1, $vf2\n"
		"cfc2 %0, $17\n"
		"cfc2.i %1, $17\n"
		: "=&r"(plain), "=&r"(interlocked) : "r"(&a), "r"(&b)
	);
	printf("mac from cfc2 %04x, from cfc2.i %04x\n", plain & 0xFFFF,
	       interlocked & 0xFFFF);
}

// Which bits of each control register a ctc2 can put back.
static void testWritable() {
	static const u32 values[] = {0x00000000, 0xFFFFFFFF, 0x0000FFFF, 0x00000FFF,
	                             0x00FFFFFF, 0x12345678};

	printf("ctc2 then cfc2:\n");
	for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
		u32 status = 0;
		u32 mac = 0;
		u32 clipping = 0;
		asm volatile (
			"vnop\n"
			"ctc2 %3, $16\n"
			"ctc2 %3, $17\n"
			"ctc2 %3, $18\n"
			"vnop\n"
			"sync.p\n"
			"cfc2 %0, $16\n"
			"cfc2 %1, $17\n"
			"cfc2 %2, $18\n"
			: "=&r"(status), "=&r"(mac), "=&r"(clipping) : "r"(values[v])
		);
		printf("  wrote %08x: st %08x mac %08x clip %08x\n", values[v], status, mac,
		       clipping);
	}
}

// The status register keeps a sticky copy of what mac has raised, which only
// a write should be able to clear.
static void testSticky() {
	Quad a, b, out;
	splat(out, junkBits);

	printf("Status across a sequence:\n");
	lanes(a, mixedLanes);
	splat(b, 0x7F7FFFFF);

	u32 afterMul = 0;
	u32 afterZero = 0;
	u32 afterClear = 0;
	clearFlags();
	asm volatile (
		"lqc2 $vf1, 0(%3)\n"
		"lqc2 $vf2, 0(%4)\n"
		"vmul.xyzw $vf3, $vf1, $vf2\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"sync.p\n"
		"cfc2 %0, $16\n"
		"vmul.xyzw $vf3, $vf0, $vf0\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"vnop\n"
		"sync.p\n"
		"cfc2 %1, $16\n"
		"ctc2 $0, $16\n"
		"vnop\n"
		"sync.p\n"
		"cfc2 %2, $16\n"
		: "=&r"(afterMul), "=&r"(afterZero), "=&r"(afterClear)
		: "r"(&a), "r"(&b)
	);
	printf("  after an overflowing mul %04x, after a quiet one %04x, after ctc2 0 %04x\n",
	       afterMul & 0xFFFF, afterZero & 0xFFFF, afterClear & 0xFFFF);
}

// The clipping register keeps a history of the last four comparisons.
static void testClipHistory() {
	static const u32 outside[4] = {0x40000000, 0xC0000000, 0x40000000, 0x3F800000};
	static const u32 inside[4] = {0x3E800000, 0xBE800000, 0x3E800000, 0x3F800000};

	printf("vclip history:\n");
	for (int count = 0; count <= 5; ++count) {
		Quad a, b;
		lanes(a, count % 2 ? inside : outside);
		lanes(b, inside);

		u32 clipping = 0;
		clearFlags();
		for (int i = 0; i < count; ++i) {
			asm volatile (
				"lqc2 $vf1, 0(%0)\n"
				"lqc2 $vf2, 0(%1)\n"
				"vclipw.xyz $vf1, $vf2\n"
				: : "r"(&a), "r"(&b)
			);
		}
		asm volatile (
			"vnop\n"
			"vnop\n"
			"vnop\n"
			"vnop\n"
			"sync.p\n"
			"cfc2 %0, $18\n"
			: "=r"(clipping)
		);
		printf("  %d clips: %06x\n", count, clipping & 0xFFFFFF);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testDelay();
	testInterlocked();
	testWritable();
	testSticky();
	testClipHistory();

	printf("-- TEST END\n");
	return 0;
}
