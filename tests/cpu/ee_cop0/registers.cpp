#include <common-ee.h>
#include <kernel.h>

// Which of the coprocessor zero registers exist, which bits of each a write
// keeps, and which are read only.  The output is buffered, since a write to
// Status can take the console with it until the register is put back.

#define READ_COP0(N) \
static u32 read##N() { \
	u32 value; \
	asm volatile ("mfc0 %0, $" #N "\n" "sync.p\n" : "=r"(value)); \
	return value; \
}

#define WRITE_COP0(N) \
static void write##N(u32 value) { \
	asm volatile ("mtc0 %0, $" #N "\n" "sync.p\n" : : "r"(value)); \
}

#define COP0_PAIR(N) READ_COP0(N) WRITE_COP0(N)

COP0_PAIR(0)
COP0_PAIR(1)
COP0_PAIR(2)
COP0_PAIR(3)
COP0_PAIR(4)
COP0_PAIR(5)
COP0_PAIR(6)
COP0_PAIR(7)
COP0_PAIR(8)
COP0_PAIR(9)
COP0_PAIR(10)
COP0_PAIR(11)
COP0_PAIR(12)
COP0_PAIR(13)
COP0_PAIR(14)
COP0_PAIR(15)
COP0_PAIR(16)
COP0_PAIR(17)
COP0_PAIR(18)
COP0_PAIR(19)
COP0_PAIR(20)
COP0_PAIR(21)
COP0_PAIR(22)
COP0_PAIR(23)
COP0_PAIR(24)
COP0_PAIR(25)
COP0_PAIR(26)
COP0_PAIR(27)
COP0_PAIR(28)
COP0_PAIR(29)
COP0_PAIR(30)
COP0_PAIR(31)

typedef u32 (*ReadFunction)();
typedef void (*WriteFunction)(u32);

struct Register {
	const char *name;
	ReadFunction read;
	WriteFunction write;
	bool safeToWrite;
};

// Status, Cause, EPC, Count and Compare drive the parts of the machine the
// test itself runs on, so they are read but never written.
static const Register registers[] = {
	{"Index",     &read0,  &write0,  true},
	{"Random",    &read1,  &write1,  false},
	{"EntryLo0",  &read2,  &write2,  true},
	{"EntryLo1",  &read3,  &write3,  true},
	{"Context",   &read4,  &write4,  true},
	{"PageMask",  &read5,  &write5,  true},
	{"Wired",     &read6,  &write6,  false},
	{"reserved7", &read7,  &write7,  true},
	{"BadVAddr",  &read8,  &write8,  false},
	{"Count",     &read9,  &write9,  false},
	{"EntryHi",   &read10, &write10, true},
	{"Compare",   &read11, &write11, false},
	{"Status",    &read12, &write12, false},
	{"Cause",     &read13, &write13, false},
	{"EPC",       &read14, &write14, false},
	{"PRId",      &read15, &write15, false},
	{"Config",    &read16, &write16, false},
	{"reserved17", &read17, &write17, true},
	{"reserved18", &read18, &write18, true},
	{"reserved19", &read19, &write19, true},
	{"reserved20", &read20, &write20, true},
	{"reserved21", &read21, &write21, true},
	{"reserved22", &read22, &write22, true},
	{"BadPAddr",  &read23, &write23, false},
	{"Debug",     &read24, &write24, false},
	{"Perf",      &read25, &write25, false},
	{"reserved26", &read26, &write26, true},
	{"reserved27", &read27, &write27, true},
	{"TagLo",     &read28, &write28, true},
	{"TagHi",     &read29, &write29, true},
	{"ErrorEPC",  &read30, &write30, true},
	{"reserved31", &read31, &write31, true},
};

static const int registerCount = sizeof(registers) / sizeof(registers[0]);

static void testRead() {
	schedf("Initial values:\n");
	for (int i = 0; i < registerCount; ++i) {
		schedf("  $%-2d %-10s %08x\n", i, registers[i].name, registers[i].read());
	}
}

// Which bits a write keeps, for the registers that are not load bearing.
static void testWrite() {
	static const u32 patterns[] = {0xFFFFFFFF, 0x00000000, 0xAAAAAAAA, 0x55555555};

	schedf("Write and read back:\n");
	for (int i = 0; i < registerCount; ++i) {
		if (!registers[i].safeToWrite) {
			schedf("  $%-2d %-10s not written\n", i, registers[i].name);
			continue;
		}
		const u32 saved = registers[i].read();
		schedf("  $%-2d %-10s", i, registers[i].name);
		for (unsigned p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
			registers[i].write(patterns[p]);
			schedf(" %08x", registers[i].read());
		}
		registers[i].write(saved);
		schedf("\n");
	}
}

// PRId names the processor and Config its cache layout, and neither moves.
static void testIdentity() {
	const u32 prid = read15();
	const u32 config = read16();
	schedf("PRId %08x, revision %02x, implementation %02x\n", prid, prid & 0xFF,
	       (prid >> 8) & 0xFF);
	schedf("Config %08x\n", config);
}

// Count runs off the bus clock, so the only reproducible thing about it is
// that two reads differ and that a third is not lower than the first.
static void testCount() {
	const u32 first = read9();
	for (volatile int i = 0; i < 10000; ++i) {
		continue;
	}
	const u32 second = read9();
	schedf("Count moved: %s\n", first != second ? "yes" : "no");
}

// The status bits the kernel leaves set at entry, which say what a program
// starts with.
static void testStatusBits() {
	const u32 status = read12();
	schedf("Status %08x: IE %d EXL %d ERL %d KSU %d IM %02x BEV %d EIE %d EDI %d CU %x\n",
	       status, status & 1, (status >> 1) & 1, (status >> 2) & 1,
	       (status >> 3) & 3, (status >> 10) & 0xFF, (status >> 22) & 1,
	       (status >> 16) & 1, (status >> 17) & 1, (status >> 28) & 0xF);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testRead();
	testWrite();
	testIdentity();
	testCount();
	testStatusBits();

	flushschedf();

	printf("-- TEST END\n");
	return 0;
}
