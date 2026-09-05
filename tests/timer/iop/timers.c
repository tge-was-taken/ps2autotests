#include <common-iop.h>

// The six counters this processor has, which differ from each other in width
// and in what they can count.  Nothing prints a count, since that would be a
// time: what is recorded is what a write reads back and whether the value
// moves at all.

struct Counter {
	const char *name;
	volatile u32 *base;
};

static const struct Counter counters[] = {
	{"T0", (volatile u32 *)0x1F801100},
	{"T1", (volatile u32 *)0x1F801110},
	{"T2", (volatile u32 *)0x1F801120},
	{"T3", (volatile u32 *)0x1F801480},
	{"T4", (volatile u32 *)0x1F801490},
	{"T5", (volatile u32 *)0x1F8014A0},
};

static const int counterCount = sizeof(counters) / sizeof(counters[0]);

static volatile u32 *count(const struct Counter *counter) { return counter->base; }
static volatile u32 *mode(const struct Counter *counter) { return counter->base + 1; }
static volatile u32 *target(const struct Counter *counter) { return counter->base + 2; }

static void spin(int iterations) {
	volatile int i;
	for (i = 0; i < iterations; ++i) {
		continue;
	}
}

static void testCountWidth(void) {
	static const u32 values[] = {0x00000000, 0x0000FFFF, 0x00010000, 0xFFFFFFFF};
	int i;
	unsigned v;

	printf("Count readback with the counter stopped:\n");
	for (i = 0; i < counterCount; ++i) {
		*mode(&counters[i]) = 0;
		printf("  %s:", counters[i].name);
		for (v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			*count(&counters[i]) = values[v];
			printf(" %08x", *count(&counters[i]));
		}
		printf("\n");
	}
}

static void testTargetWidth(void) {
	static const u32 values[] = {0x00000000, 0x0000FFFF, 0x00010000, 0xFFFFFFFF};
	int i;
	unsigned v;

	printf("Target readback:\n");
	for (i = 0; i < counterCount; ++i) {
		printf("  %s:", counters[i].name);
		for (v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
			*target(&counters[i]) = values[v];
			printf(" %08x", *target(&counters[i]));
		}
		printf("\n");
	}
}

static void testModeBits(void) {
	int i;

	printf("Mode readback:\n");
	for (i = 0; i < counterCount; ++i) {
		u32 all, none;
		*mode(&counters[i]) = 0xFFFFFFFF;
		all = *mode(&counters[i]);
		*mode(&counters[i]) = 0x00000000;
		none = *mode(&counters[i]);
		printf("  %s: all %08x none %08x\n", counters[i].name, all, none);
	}
}

// Whether the value moves once the counter is running, per clock source.
static void testCounting(void) {
	static const u32 sources[] = {0, 1, 2, 3};
	int i;
	unsigned s;

	printf("Counting:\n");
	for (i = 0; i < counterCount; ++i) {
		for (s = 0; s < sizeof(sources) / sizeof(sources[0]); ++s) {
			u32 first, second;
			*mode(&counters[i]) = 0;
			*count(&counters[i]) = 0;
			// The enable bit sits at nine, the source above it.
			*mode(&counters[i]) = (sources[s] << 8) | (1 << 7);
			spin(20000);
			first = *count(&counters[i]);
			spin(20000);
			second = *count(&counters[i]);
			*mode(&counters[i]) = 0;

			printf("  %s source %u: started %s, kept going %s\n", counters[i].name,
			       sources[s], first != 0 ? "yes" : "no",
			       second != first ? "yes" : "no");
		}
	}
}

// Reading the mode register is documented as clearing its status bits.
static void testModeClearsOnRead(void) {
	int i;

	printf("Reading the mode register twice:\n");
	for (i = 0; i < counterCount; ++i) {
		u32 first, second;
		*mode(&counters[i]) = 0;
		*count(&counters[i]) = 0;
		*target(&counters[i]) = 0x40;
		*mode(&counters[i]) = (1 << 7) | (1 << 4) | (1 << 6);
		spin(40000);
		first = *mode(&counters[i]);
		second = *mode(&counters[i]);
		*mode(&counters[i]) = 0;
		printf("  %s: first %08x second %08x\n", counters[i].name, first, second);
	}
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testCountWidth();
	testTargetWidth();
	testModeBits();
	testCounting();
	testModeClearsOnRead();

	printf("-- TEST END\n");
	return 0;
}
