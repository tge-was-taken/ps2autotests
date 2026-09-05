#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>
#include <thevent.h>

// The wait modes an event flag offers: any bit or all of them, and whether the
// bits are cleared on the way out.  The existing tests cover deleting and
// polling; this one is about the four combinations in between.

#define STACK_SIZE 0x800

static s32 flagId;
static s32 waiterId;
static u32 waiterResult;
static u32 waiterBits;
static int waiterFinished;

static s32 makeFlag(u32 initial, u32 attr) {
	iop_event_t info;
	memset(&info, 0, sizeof(info));
	info.attr = attr;
	info.option = 0;
	info.bits = initial;
	return CreateEventFlag(&info);
}

static void printStatus(const char *what, s32 id) {
	iop_event_info_t info;
	memset(&info, 0, sizeof(info));
	if (ReferEventFlagStatus(id, &info) < 0) {
		printf("  %-24s refer failed\n", what);
		return;
	}
	printf("  %-24s bits %08x attr %08x waiters %d\n", what, info.currBits,
	       info.attr, info.numThreads);
}

// Polling, which never blocks, across every mode and pattern.
static void testPoll(void) {
	static const u32 patterns[] = {0x00000000, 0x00000001, 0x00000003, 0x0000000F,
	                               0x80000000, 0xFFFFFFFF};
	static const u32 waits[] = {0x00000001, 0x00000003, 0x0000000F, 0x00000010,
	                            0xFFFFFFFF};
	static const u32 modes[] = {WEF_OR, WEF_AND, WEF_OR | WEF_CLEAR,
	                            WEF_AND | WEF_CLEAR};
	static const char *const modeNames[] = {"or", "and", "or+clear", "and+clear"};
	unsigned p, w, m;

	printf("PollEventFlag:\n");
	for (m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
		for (p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
			for (w = 0; w < sizeof(waits) / sizeof(waits[0]); ++w) {
				u32 out = 0xDEADBEEF;
				s32 result;
				const s32 id = makeFlag(patterns[p], EA_MULTI);
				if (id < 0) {
					continue;
				}
				result = PollEventFlag(id, waits[w], modes[m], &out);
				printf("  %-9s have %08x want %08x: result %d out %08x", modeNames[m],
				       patterns[p], waits[w], result, out);
				{
					iop_event_info_t info;
					memset(&info, 0, sizeof(info));
					ReferEventFlagStatus(id, &info);
					printf(" left %08x\n", info.currBits);
				}
				DeleteEventFlag(id);
			}
		}
	}
}

// Setting and clearing, which are the two ways the bits move.
static void testSetAndClear(void) {
	const s32 id = makeFlag(0, EA_MULTI);

	printf("Set and clear:\n");
	if (id < 0) {
		printf("  could not create: %d\n", id);
		return;
	}
	printStatus("created with 0", id);
	SetEventFlag(id, 0x0000000F);
	printStatus("set 0000000f", id);
	SetEventFlag(id, 0x000000F0);
	printStatus("set 000000f0", id);
	ClearEventFlag(id, 0xFFFFFF0F);
	printStatus("cleared all but f0", id);
	ClearEventFlag(id, 0x00000000);
	printStatus("cleared with 0", id);
	SetEventFlag(id, 0xFFFFFFFF);
	printStatus("set every bit", id);
	DeleteEventFlag(id);
}

// A thread blocked on a flag, and what reaches it when the bits arrive.
static void waiterProc(void *arg) {
	waiterBits = 0xDEADBEEF;
	waiterResult = WaitEventFlag(flagId, (u32)arg, WEF_AND | WEF_CLEAR, &waiterBits);
	waiterFinished = 1;
	SleepThread();
}

static void testBlockedWait(void) {
	iop_thread_t param;

	flagId = makeFlag(0, EA_MULTI);
	waiterFinished = 0;
	memset(&param, 0, sizeof(param));
	param.attr = TH_C;
	param.thread = &waiterProc;
	param.priority = 0x30;
	param.stacksize = STACK_SIZE;
	waiterId = CreateThread(&param);

	printf("A thread waiting for two bits:\n");
	if (waiterId < 0 || flagId < 0) {
		printf("  could not set up: thread %d flag %d\n", waiterId, flagId);
		return;
	}
	StartThread(waiterId, (void *)0x00000003);
	DelayThread(10000);
	printStatus("while waiting", flagId);
	printf("  finished %d\n", waiterFinished);

	SetEventFlag(flagId, 0x00000001);
	DelayThread(10000);
	printStatus("after the first bit", flagId);
	printf("  finished %d\n", waiterFinished);

	SetEventFlag(flagId, 0x00000002);
	DelayThread(10000);
	printStatus("after the second bit", flagId);
	printf("  finished %d, result %d, bits %08x\n", waiterFinished, waiterResult,
	       waiterBits);

	TerminateThread(waiterId);
	DeleteThread(waiterId);
	DeleteEventFlag(flagId);
	flagId = -1;
	waiterId = -1;
}

// The attribute that decides whether more than one thread may wait.
static void testAttributes(void) {
	static const u32 attributes[] = {0, EA_MULTI, 0xFFFFFFFF};
	unsigned i;

	printf("Create with each attribute:\n");
	for (i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i) {
		const s32 id = makeFlag(0x12345678, attributes[i]);
		printf("  attr %08x: id %d\n", attributes[i], id);
		if (id >= 0) {
			printStatus("  status", id);
			DeleteEventFlag(id);
		}
	}
}

// Calls against an identifier the kernel never issued.
static void testBadIds(void) {
	static const s32 ids[] = {-1, 0, 1, 0x100, 0x7FFFFFFF};
	unsigned i;

	printf("Calls with a bad identifier:\n");
	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
		u32 out = 0;
		printf("  %08x: set %d clear %d poll %d delete %d\n", ids[i],
		       SetEventFlag(ids[i], 1), ClearEventFlag(ids[i], 0),
		       PollEventFlag(ids[i], 1, WEF_OR, &out), DeleteEventFlag(ids[i]));
	}
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testAttributes();
	testSetAndClear();
	testPoll();
	testBlockedWait();
	testBadIds();

	printf("-- TEST END\n");
	return 0;
}
