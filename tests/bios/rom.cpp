#include <common-ee.h>
#include <kernel.h>
#include <string.h>

// What the read only memory holds: the directory at its start, the version
// string, and the configuration the kernel keeps for the machine.  An emulated
// kernel has to answer a file query the same way, so the names and sizes are
// what matters rather than the contents.

struct RomEntry {
	char name[10];
	u16 extraInfo;
	u32 size;
};

static const u8 *const romStart = (const u8 *)0xBFC00000;

// The reset code comes first and the directory sits behind it, so the entry
// called RESET is found by scanning rather than assumed to be at the start.
static const RomEntry *findDirectory() {
	for (u32 at = 0; at < 0x10000; at += 16) {
		const RomEntry *entry = (const RomEntry *)(romStart + at);
		if (memcmp(entry->name, "RESET", 6) == 0) {
			return entry;
		}
	}
	return 0;
}

static u32 directoryOffset() {
	const RomEntry *entry = findDirectory();
	return entry == 0 ? 0 : (u32)((const u8 *)entry - romStart);
}

static void printName(const char *name) {
	int i;
	for (i = 0; i < 10 && name[i] != 0; ++i) {
		printf("%c", name[i]);
	}
	for (; i < 10; ++i) {
		printf(" ");
	}
}

static void testDirectory() {
	const RomEntry *entry = findDirectory();

	printf("The directory at the start of read only memory:\n");
	if (entry == 0) {
		printf("  no RESET entry in the first 64 kilobytes\n");
		return;
	}
	printf("  directory at %08x\n", directoryOffset());

	u32 offset = 0;
	int count = 0;
	for (; entry->name[0] != 0 && count < 64; ++entry, ++count) {
		printf("  ");
		printName(entry->name);
		printf(" size %8u extra %04x offset %08x\n", entry->size, entry->extraInfo,
		       offset);
		offset += (entry->size + 15) & ~15u;
	}
	printf("  %d entries, %u bytes accounted for\n", count, offset);
}

// The version entry, whose contents name the machine.
static void testVersion() {
	const RomEntry *entry = findDirectory();
	u32 offset = 0;

	printf("The version entry:\n");
	if (entry == 0) {
		printf("  no directory\n");
		return;
	}
	for (; entry->name[0] != 0; ++entry) {
		if (memcmp(entry->name, "ROMVER", 6) == 0) {
			const char *text = (const char *)(romStart + offset);
			printf("  at %08x, %u bytes: ", offset, entry->size);
			for (u32 i = 0; i < entry->size && i < 20; ++i) {
				printf("%c", text[i] >= 0x20 && text[i] < 0x7F ? text[i] : '.');
			}
			printf("\n");
			return;
		}
		offset += (entry->size + 15) & ~15u;
	}
	printf("  not present\n");
}

// The first words of the reset entry, which is where the machine starts.
static void testResetVector() {
	printf("The first eight words:\n");
	const volatile u32 *at = (const volatile u32 *)romStart;
	for (int i = 0; i < 8; ++i) {
		printf("  %08x: %08x\n", 0xBFC00000 + i * 4, at[i]);
	}
}

// The configuration block the kernel keeps, which decides the language and the
// video mode a program should use.
static void testConfiguration() {
	u32 config[2];
	memset(config, 0, sizeof(config));
	GetOsdConfigParam(config);

	printf("Configuration: %08x %08x\n", config[0], config[1]);
	printf("  spdif %d, screen %d, video %d, language %d, timezone %d\n",
	       config[0] & 1, (config[0] >> 1) & 3, (config[0] >> 3) & 1,
	       (config[0] >> 4) & 0x1F, (config[0] >> 9) & 0x7FF);
}

// The second read only region, which a retail machine has and a development
// one may not.
static void testSecondRegion() {
	static const u32 bases[] = {0xBFC00000, 0xBFF00000, 0xBE000000};

	printf("The first two words of each region:\n");
	for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
		const volatile u32 *at = (const volatile u32 *)bases[i];
		printf("  %08x: %08x %08x\n", bases[i], at[0], at[1]);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testResetVector();
	testDirectory();
	testVersion();
	testConfiguration();
	testSecondRegion();

	printf("-- TEST END\n");
	return 0;
}
