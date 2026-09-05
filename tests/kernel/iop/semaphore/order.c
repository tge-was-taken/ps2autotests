#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>
#include <thsemap.h>

// Which waiting thread a signal wakes.  A semaphore can hand the count to the
// thread that asked first or to the one with the highest priority, and which
// it is decides the order a whole program runs in.

#define WAITER_COUNT 4
#define STACK_SIZE 0x800

static char stacks[WAITER_COUNT][STACK_SIZE] __attribute__((aligned(16)));
static s32 waiterIds[WAITER_COUNT];
static s32 semaId;

static int wokenOrder[WAITER_COUNT];
static int wokenCount;

static void waiterProc(void *arg) {
	const int index = (int)arg;
	WaitSema(semaId);
	wokenOrder[wokenCount] = index;
	wokenCount++;
	SleepThread();
}

static s32 createWaiter(int index, int priority) {
	iop_thread_t param;
	memset(&param, 0, sizeof(param));
	param.attr = TH_C;
	param.thread = &waiterProc;
	param.priority = priority;
	param.stacksize = STACK_SIZE;
	param.option = 0;
	return CreateThread(&param);
}

static s32 makeSemaphore(int initial, int max) {
	iop_sema_t info;
	memset(&info, 0, sizeof(info));
	info.attr = 0;
	info.initial = initial;
	info.max = max;
	info.option = 0;
	return CreateSema(&info);
}

static void cleanup(void) {
	int i;
	for (i = 0; i < WAITER_COUNT; ++i) {
		if (waiterIds[i] >= 0) {
			TerminateThread(waiterIds[i]);
			DeleteThread(waiterIds[i]);
			waiterIds[i] = -1;
		}
	}
	if (semaId >= 0) {
		DeleteSema(semaId);
		semaId = -1;
	}
}

// Four threads all waiting, started in one order and given priorities in
// another, then woken one at a time.
static void testWakeOrder(const char *name, const int *priorities) {
	int i;

	wokenCount = 0;
	for (i = 0; i < WAITER_COUNT; ++i) {
		wokenOrder[i] = -1;
		waiterIds[i] = -1;
	}

	semaId = makeSemaphore(0, WAITER_COUNT);
	for (i = 0; i < WAITER_COUNT; ++i) {
		waiterIds[i] = createWaiter(i, priorities[i]);
		if (waiterIds[i] >= 0) {
			StartThread(waiterIds[i], (void *)i);
		}
	}
	DelayThread(10000);

	for (i = 0; i < WAITER_COUNT; ++i) {
		SignalSema(semaId);
		DelayThread(10000);
	}

	printf("%s:\n", name);
	printf("  priorities");
	for (i = 0; i < WAITER_COUNT; ++i) {
		printf(" %d", priorities[i]);
	}
	printf("\n  woken in order");
	for (i = 0; i < WAITER_COUNT; ++i) {
		printf(" %d", wokenOrder[i]);
	}
	printf("\n");

	cleanup();
}

// The counts a semaphore accepts, and what a signal past the maximum does.
static void testCounts(void) {
	static const int cases[][2] = {{0, 1}, {1, 1}, {2, 2}, {0, 0}, {1, 0},
	                               {-1, 1}, {0, -1}, {0, 0x7FFF}};
	unsigned i;

	printf("CreateSema counts:\n");
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		const s32 id = makeSemaphore(cases[i][0], cases[i][1]);
		printf("  initial %6d max %6d: %d\n", cases[i][0], cases[i][1], id);
		if (id >= 0) {
			DeleteSema(id);
		}
	}
}

// Signalling past the maximum, and polling an empty one.
static void testSignalAndPoll(void) {
	const s32 id = makeSemaphore(0, 2);

	printf("Signal and poll on a semaphore of two:\n");
	if (id < 0) {
		printf("  could not create: %d\n", id);
		return;
	}
	printf("  poll empty %d\n", PollSema(id));
	printf("  signal %d, signal %d, signal past the max %d\n", SignalSema(id),
	       SignalSema(id), SignalSema(id));
	printf("  poll %d, poll %d, poll empty %d\n", PollSema(id), PollSema(id),
	       PollSema(id));
	DeleteSema(id);
}

// Deleting a semaphore threads are waiting on, which has to release them with
// an error rather than leave them stuck.
static void testDeleteWhileWaiting(void) {
	static const int priorities[WAITER_COUNT] = {0x30, 0x30, 0x30, 0x30};
	int i;

	wokenCount = 0;
	for (i = 0; i < WAITER_COUNT; ++i) {
		wokenOrder[i] = -1;
		waiterIds[i] = -1;
	}

	semaId = makeSemaphore(0, WAITER_COUNT);
	for (i = 0; i < 2; ++i) {
		waiterIds[i] = createWaiter(i, priorities[i]);
		if (waiterIds[i] >= 0) {
			StartThread(waiterIds[i], (void *)i);
		}
	}
	DelayThread(10000);

	printf("Deleting a semaphore with two waiters: %d\n", DeleteSema(semaId));
	semaId = -1;
	DelayThread(10000);
	printf("  woken");
	for (i = 0; i < WAITER_COUNT; ++i) {
		printf(" %d", wokenOrder[i]);
	}
	printf("\n");
	cleanup();
}

// Every call against an identifier the kernel never issued.
static void testBadIds(void) {
	static const s32 ids[] = {-1, 0, 1, 0x100, 0x7FFFFFFF};
	unsigned i;

	printf("Calls with a bad identifier:\n");
	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
		printf("  %08x: signal %d poll %d delete %d\n", ids[i], SignalSema(ids[i]),
		       PollSema(ids[i]), DeleteSema(ids[i]));
	}
}

int _start(int argc, char *argv[]) {
	static const int equal[WAITER_COUNT] = {0x30, 0x30, 0x30, 0x30};
	static const int rising[WAITER_COUNT] = {0x40, 0x38, 0x30, 0x28};
	static const int falling[WAITER_COUNT] = {0x28, 0x30, 0x38, 0x40};

	printf("-- TEST BEGIN\n");

	testCounts();
	testSignalAndPoll();
	testWakeOrder("Equal priorities", equal);
	testWakeOrder("Priority rising with start order", rising);
	testWakeOrder("Priority falling with start order", falling);
	testDeleteWhileWaiting();
	testBadIds();

	printf("-- TEST END\n");
	return 0;
}
