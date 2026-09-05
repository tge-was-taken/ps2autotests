#include <common-iop.h>
#include <loadcore.h>
#include <sysclib.h>
#include <thbase.h>

// The library list every module registers into, and the boot mode list the
// kernel keeps.  What matters is the shape of the list and the names in it,
// since a module that resolves an import walks exactly this.

// A library of this test's own, so registering is measured against a name
// nothing else uses.
static int firstEntry(void) {
	return 0x1111;
}

static int secondEntry(void) {
	return 0x2222;
}

// The table the kernel takes has a trailing pointer array, so it is declared
// here with a fixed one rather than through the macro that builds it in .text.
struct TestTable {
	u32 magic;
	struct irx_export_table *next;
	u16 version;
	u16 mode;
	u8 name[8];
	void *fptrs[3];
};

static struct TestTable exports = {
	EXPORT_MAGIC,
	0,
	IRX_VER(1, 1),
	0,
	{'t', 'e', 's', 't', 'l', 'i', 'b', 0},
	{(void *)&firstEntry, (void *)&secondEntry, 0},
};

static struct irx_export_table *table(struct TestTable *of) {
	return (struct irx_export_table *)of;
}

static void printName(const char *name) {
	int i;
	for (i = 0; i < 8 && name[i] != 0; ++i) {
		printf("%c", name[i]);
	}
	for (; i < 8; ++i) {
		printf(" ");
	}
}

// The list as the kernel holds it, walked from the head.
static void testWalkTheList(void) {
	iop_library_t *library = (iop_library_t *)QueryLibraryEntryTable(0);
	int count = 0;

	printf("The library list:\n");
	if (library == 0) {
		printf("  the head is null\n");
		return;
	}

	while (library != 0 && count < 128) {
		printf("  %3d at %08x: ", count, (u32)library);
		printName(library->name);
		printf(" version %04x flags %04x\n", library->version, library->flags);
		library = library->prev;
		count++;
	}
	printf("  %d entries\n", count);
}

// Registering a library of this test's own, and taking it away again.
static void testRegister(void) {
	printf("Registering a library:\n");

	printf("  register %d\n", RegisterLibraryEntries(table(&exports)));
	printf("  register again %d\n", RegisterLibraryEntries(table(&exports)));
	printf("  release %d\n", ReleaseLibraryEntries(table(&exports)));
	printf("  release again %d\n", ReleaseLibraryEntries(table(&exports)));
}

// The list with this test's library in it, which says where a new one goes.
static void testWhereItLands(void) {
	iop_library_t *library;
	int position = -1;
	int count = 0;

	RegisterLibraryEntries(table(&exports));

	library = (iop_library_t *)QueryLibraryEntryTable(0);
	while (library != 0 && count < 128) {
		if (memcmp(library->name, "testlib", 7) == 0) {
			position = count;
		}
		library = library->prev;
		count++;
	}
	printf("A newly registered library sits at %d of %d\n", position, count);

	ReleaseLibraryEntries(table(&exports));
}

// A table whose magic or version is wrong, which the kernel has to refuse.
static void testBadTables(void) {
	struct TestTable attempt;

	printf("Tables the kernel should refuse:\n");

	memcpy(&attempt, &exports, sizeof(attempt));
	attempt.magic = 0;
	printf("  no magic: %d\n", RegisterLibraryEntries(table(&attempt)));

	memcpy(&attempt, &exports, sizeof(attempt));
	attempt.magic = 0x41C00001;
	printf("  wrong magic: %d\n", RegisterLibraryEntries(table(&attempt)));

	memcpy(&attempt, &exports, sizeof(attempt));
	memcpy(attempt.name, "testli2", 8);
	attempt.version = 0;
	printf("  version zero: %d\n", RegisterLibraryEntries(table(&attempt)));
	ReleaseLibraryEntries(table(&attempt));

	printf("  a null table: %d\n", RegisterLibraryEntries(0));
}

// The boot mode list, which is how a module learns what the machine was asked
// to do.
static void testBootModes(void) {
	int mode;

	printf("The boot modes:\n");
	for (mode = 0; mode < 12; ++mode) {
		int *const found = QueryBootMode(mode);
		printf("  %2d: %08x", mode, (u32)found);
		if (found != 0) {
			printf(" -> %08x %08x", found[0], found[1]);
		}
		printf("\n");
	}
}

// Looking up a library by name, which is what an import table resolves
// against.
static void testFindByName(void) {
	static const char *const names[] = {"loadcore", "intrman", "thbase",
	                                    "sysclib", "sysmem", "stdio",
	                                    "nosuchlb"};
	unsigned i;

	printf("Looking for each name in the list:\n");
	for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		iop_library_t *library = (iop_library_t *)QueryLibraryEntryTable(0);
		int found = 0;
		int count = 0;

		while (library != 0 && count < 128) {
			if (memcmp(library->name, names[i], strlen(names[i])) == 0) {
				found = 1;
				printf("  %-9s version %04x flags %04x at %08x\n", names[i],
				       library->version, library->flags, (u32)library);
				break;
			}
			library = library->prev;
			count++;
		}
		if (!found) {
			printf("  %-9s not in the list\n", names[i]);
		}
	}
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testBootModes();
	testWalkTheList();
	testFindByName();
	testRegister();
	testWhereItLands();
	testBadTables();

	printf("-- TEST END\n");
	return 1;
}
