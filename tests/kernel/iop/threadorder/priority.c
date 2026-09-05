#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>

// The order ready threads run in, and the two calls that change it.  A
// scheduler that picks differently makes a program that works here fail there,
// so the sequence is the thing to record.

#define RUNNER_COUNT 4
#define STACK_SIZE 0x800
#define MARK_LIMIT 32

static s32 runnerIds[RUNNER_COUNT];
static int marks[MARK_LIMIT];
static int markCount;

static void mark(int value) {
	if (markCount < MARK_LIMIT) {
		marks[markCount] = value;
		markCount++;
	}
}

static void printMarks(const char *what) {
	int i;
	printf("  %-28s", what);
	for (i = 0; i < markCount; ++i) {
		printf(" %d", marks[i]);
	}
	printf("\n");
}

// Each runner leaves its number behind twice, so a scheduler that interleaves
// looks different from one that runs each to its end.
static void runnerProc(void *arg) {
	const int index = (int)arg;
	mark(index);
	mark(index);
	SleepThread();
}

static s32 createRunner(int priority) {
	iop_thread_t param;
	memset(&param, 0, sizeof(param));
	param.attr = TH_C;
	param.thread = &runnerProc;
	param.priority = priority;
	param.stacksize = STACK_SIZE;
	param.option = 0;
	return CreateThread(&param);
}

static void cleanup(void) {
	int i;
	for (i = 0; i < RUNNER_COUNT; ++i) {
		if (runnerIds[i] >= 0) {
			TerminateThread(runnerIds[i]);
			DeleteThread(runnerIds[i]);
			runnerIds[i] = -1;
		}
	}
}

static void testStartOrder(const char *name, const int *priorities) {
	int i;

	markCount = 0;
	for (i = 0; i < RUNNER_COUNT; ++i) {
		runnerIds[i] = -1;
	}
	for (i = 0; i < RUNNER_COUNT; ++i) {
		runnerIds[i] = createRunner(priorities[i]);
	}
	for (i = 0; i < RUNNER_COUNT; ++i) {
		if (runnerIds[i] >= 0) {
			StartThread(runnerIds[i], (void *)i);
		}
	}
	DelayThread(20000);

	printf("%s:\n", name);
	printf("  priorities                 ");
	for (i = 0; i < RUNNER_COUNT; ++i) {
		printf(" %d", priorities[i]);
	}
	printf("\n");
	printMarks("ran in order");
	cleanup();
}

// Changing a thread's priority while it is ready, which should move it in the
// queue.
static void testChangeWhileReady(void) {
	int i;
	static const int equal[RUNNER_COUNT] = {0x40, 0x40, 0x40, 0x40};

	markCount = 0;
	for (i = 0; i < RUNNER_COUNT; ++i) {
		runnerIds[i] = createRunner(equal[i]);
	}
	// The last one is raised above the rest before any of them run.
	if (runnerIds[RUNNER_COUNT - 1] >= 0) {
		ChangeThreadPriority(runnerIds[RUNNER_COUNT - 1], 0x20);
	}
	for (i = 0; i < RUNNER_COUNT; ++i) {
		if (runnerIds[i] >= 0) {
			StartThread(runnerIds[i], (void *)i);
		}
	}
	DelayThread(20000);

	printf("The last thread raised before any run:\n");
	printMarks("ran in order");
	cleanup();
}

// The call that moves the head of one priority to the back of its own queue.
static void testRotate(void) {
	int i;
	static const int equal[RUNNER_COUNT] = {0x40, 0x40, 0x40, 0x40};

	markCount = 0;
	for (i = 0; i < RUNNER_COUNT; ++i) {
		runnerIds[i] = createRunner(equal[i]);
	}
	for (i = 0; i < RUNNER_COUNT; ++i) {
		if (runnerIds[i] >= 0) {
			StartThread(runnerIds[i], (void *)i);
		}
	}
	printf("RotateThreadReadyQueue:\n");
	printf("  at priority 0x40: %d\n", RotateThreadReadyQueue(0x40));
	printf("  at priority 0x10: %d\n", RotateThreadReadyQueue(0x10));
	printf("  at priority 0:    %d\n", RotateThreadReadyQueue(0));
	printf("  at priority 0x7F: %d\n", RotateThreadReadyQueue(0x7F));
	DelayThread(20000);
	printMarks("ran in order");
	cleanup();
}

// Which priorities the kernel will accept, and what a thread's status says
// after each change.
static void testPriorityRange(void) {
	static const int priorities[] = {-1, 0, 1, 2, 0x40, 0x7E, 0x7F, 0x80, 0x100};
	unsigned i;
	const s32 id = createRunner(0x40);

	printf("ChangeThreadPriority range:\n");
	if (id < 0) {
		printf("  could not create a thread: %d\n", id);
		return;
	}
	for (i = 0; i < sizeof(priorities) / sizeof(priorities[0]); ++i) {
		iop_thread_info_t info;
		const s32 result = ChangeThreadPriority(id, priorities[i]);
		memset(&info, 0, sizeof(info));
		ReferThreadStatus(id, &info);
		printf("  %5d: result %d, current %d, initial %d\n", priorities[i], result,
		       info.currentPriority, info.initPriority);
	}
	DeleteThread(id);
}

// Calls against an identifier the kernel never issued.
static void testBadIds(void) {
	static const s32 ids[] = {-1, 0, 1, 0x100, 0x7FFFFFFF};
	unsigned i;

	printf("Calls with a bad identifier:\n");
	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
		printf("  %08x: start %d wakeup %d release %d delete %d\n", ids[i],
		       StartThread(ids[i], 0), WakeupThread(ids[i]),
		       ReleaseWaitThread(ids[i]), DeleteThread(ids[i]));
	}
}

int _start(int argc, char *argv[]) {
	static const int equal[RUNNER_COUNT] = {0x40, 0x40, 0x40, 0x40};
	static const int rising[RUNNER_COUNT] = {0x50, 0x48, 0x40, 0x38};
	static const int falling[RUNNER_COUNT] = {0x38, 0x40, 0x48, 0x50};

	printf("-- TEST BEGIN\n");

	testStartOrder("Equal priorities", equal);
	testStartOrder("Priority rising with start order", rising);
	testStartOrder("Priority falling with start order", falling);
	testChangeWhileReady();
	testRotate();
	testPriorityRange();
	testBadIds();

	printf("-- TEST END\n");
	return 0;
}
