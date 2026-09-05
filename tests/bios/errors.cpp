#include <common-ee.h>
#include <kernel.h>
#include <string.h>

// What the kernel returns for arguments it cannot accept.  An emulated kernel
// has to answer the same way, and a wrong code here is a class of bug that
// only shows up much later in a game.

static const s32 badIds[] = {-1, 0, 1, 2, 0x7F, 0x100, 0x1000, 0x7FFFFFFF};

// The kernel hands out small numbers, so the first few of those are objects
// something else owns.  A call that destroys one is only made on the ids that
// cannot name anything.
static const s32 unusedIds[] = {-1, 0x7F, 0x100, 0x1000, 0x7FFFFFFF};
static const int unusedIdCount = sizeof(unusedIds) / sizeof(unusedIds[0]);
static const int badIdCount = sizeof(badIds) / sizeof(badIds[0]);

// The kernel writes through the buffer before it checks the id, so the sweep
// hands it none and only the running thread's own id gets one.
static void testReferWithBadId() {
	printf("ReferThreadStatus with a bad id and no buffer:\n");
	for (int i = 0; i < badIdCount; ++i) {
		printf("  %08x: %d\n", badIds[i], ReferThreadStatus(badIds[i], 0));
	}

	ee_thread_status_t status;
	memset(&status, 0, sizeof(status));
	const s32 self = GetThreadId();
	printf("ReferThreadStatus on this thread: %d\n",
	       ReferThreadStatus(self, &status));
	printf("  status %d, priority %d, entry %08x, stack %08x %d\n", status.status,
	       status.current_priority, (u32)status.func, (u32)status.stack,
	       status.stack_size);
}

static void testThreadErrors() {
	printf("Thread calls with a bad id:\n");
	for (int i = 0; i < badIdCount; ++i) {
		// Deleting or terminating the running thread would end the test, so
		// the id of this thread is left out of the list above.
		printf("  %08x: suspend %d resume %d wakeup %d cancel %d\n", badIds[i],
		       SuspendThread(badIds[i]), ResumeThread(badIds[i]),
		       WakeupThread(badIds[i]), CancelWakeupThread(badIds[i]));
	}

	printf("ChangeThreadPriority with a bad id or priority:\n");
	for (int i = 0; i < badIdCount; ++i) {
		printf("  id %08x: %d\n", badIds[i], ChangeThreadPriority(badIds[i], 0x40));
	}
	const s32 self = GetThreadId();
	static const int priorities[] = {-1, 0, 1, 0x7F, 0x80, 0x100};
	for (unsigned i = 0; i < sizeof(priorities) / sizeof(priorities[0]); ++i) {
		const s32 result = ChangeThreadPriority(self, priorities[i]);
		printf("  priority %4d: %d\n", priorities[i], result);
		ChangeThreadPriority(self, 0x40);
	}
}

// A thread the test creates and then deletes, so the id is one the kernel has
// seen rather than one it never issued.
static void testStaleId() {
	static char stack[0x2000] __attribute__((aligned(16)));

	ee_thread_t param;
	memset(&param, 0, sizeof(param));
	param.func = (void *)&testStaleId;
	param.initial_priority = 0x50;
	param.stack = stack;
	param.stack_size = sizeof(stack);

	const s32 id = CreateThread(&param);
	printf("A created thread: id %d\n", id);
	if (id >= 0) {
		printf("  delete %d, delete again %d, refer after %d\n", DeleteThread(id),
		       DeleteThread(id), ReferThreadStatus(id, 0));
	}
}

static char sharedStack[0x2000] __attribute__((aligned(16)));

static ee_thread_t goodParam() {
	ee_thread_t param;
	memset(&param, 0, sizeof(param));
	param.func = (void *)&testStaleId;
	param.initial_priority = 0x50;
	param.stack = sharedStack;
	param.stack_size = sizeof(sharedStack);
	return param;
}

static void testCreatePriorities() {
	printf("CreateThread with a bad priority:\n");

	const ee_thread_t param = goodParam();
	static const int priorities[] = {0, 1, 0x7F, 0x80, -1};
	for (unsigned i = 0; i < sizeof(priorities) / sizeof(priorities[0]); ++i) {
		ee_thread_t attempt = param;
		attempt.initial_priority = priorities[i];
		const s32 id = CreateThread(&attempt);
		printf("  priority %4d: %d\n", priorities[i], id);
		if (id >= 0) {
			DeleteThread(id);
		}
	}
}

// The kernel writes through the stack it is handed before it runs the thread,
// so a stack it cannot write to faults inside CreateThread itself.
static void testCreateBadBuffers() {
	printf("CreateThread with a stack or entry point it cannot use:\n");

	const ee_thread_t param = goodParam();

	static const u32 sizes[] = {0, 1, 16, 0x100};
	for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		ee_thread_t attempt = param;
		attempt.stack_size = sizes[i];
		const s32 id = CreateThread(&attempt);
		printf("  stack size %4u: %d\n", sizes[i], id);
		if (id >= 0) {
			DeleteThread(id);
		}
	}

	{
		ee_thread_t attempt = param;
		attempt.func = 0;
		const s32 id = CreateThread(&attempt);
		printf("  null entry point: %d\n", id);
		if (id >= 0) {
			DeleteThread(id);
		}
	}
	{
		ee_thread_t attempt = param;
		attempt.stack = 0;
		const s32 id = CreateThread(&attempt);
		printf("  null stack: %d\n", id);
		if (id >= 0) {
			DeleteThread(id);
		}
	}
}

static void testSemaphoreErrors() {
	printf("Semaphore calls with a bad id:\n");
	for (int i = 0; i < badIdCount; ++i) {
		printf("  %08x: signal %d poll %d\n", badIds[i], SignalSema(badIds[i]),
		       PollSema(badIds[i]));
	}
	printf("Deleting an id the kernel cannot have issued:\n");
	for (int i = 0; i < unusedIdCount; ++i) {
		printf("  %08x: %d\n", unusedIds[i], DeleteSema(unusedIds[i]));
	}

	printf("CreateSema with bad counts:\n");
	static const int counts[][2] = {{0, 1}, {1, 1}, {2, 1}, {-1, 1}, {0, 0},
	                                {0, -1}, {0, 0x7FFF}, {0x7FFF, 0x7FFF}};
	for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
		ee_sema_t info;
		memset(&info, 0, sizeof(info));
		info.init_count = counts[i][0];
		info.max_count = counts[i][1];
		const s32 id = CreateSema(&info);
		printf("  init %6d max %6d: %d\n", counts[i][0], counts[i][1], id);
		if (id >= 0) {
			DeleteSema(id);
		}
	}

	{
		ee_sema_t info;
		memset(&info, 0, sizeof(info));
		info.init_count = 1;
		info.max_count = 1;
		const s32 id = CreateSema(&info);
		printf("A created semaphore: id %d\n", id);
		if (id >= 0) {
			printf("  poll %d, poll again %d, signal %d, signal again %d\n",
			       PollSema(id), PollSema(id), SignalSema(id), SignalSema(id));
			printf("  delete %d, delete again %d\n", DeleteSema(id), DeleteSema(id));
		}
	}
}

static void testAlarmErrors() {
	printf("ReleaseAlarm with a bad id:\n");
	for (int i = 0; i < badIdCount; ++i) {
		printf("  %08x: %d\n", badIds[i], ReleaseAlarm(badIds[i]));
	}

	printf("SetAlarm with a null callback: %d\n", SetAlarm(100, 0, 0));
}

static void testInterruptErrors() {
	static const int causes[] = {-1, 0, 1, 14, 15, 16, 31, 0x7FFFFFFF};

	printf("EnableIntc and DisableIntc:\n");
	for (unsigned i = 0; i < sizeof(causes) / sizeof(causes[0]); ++i) {
		const int enabled = EnableIntc(causes[i]);
		const int disabled = DisableIntc(causes[i]);
		printf("  cause %11d: enable %d disable %d\n", causes[i], enabled, disabled);
	}

	printf("EnableDmac and DisableDmac:\n");
	for (unsigned i = 0; i < sizeof(causes) / sizeof(causes[0]); ++i) {
		const int enabled = EnableDmac(causes[i]);
		const int disabled = DisableDmac(causes[i]);
		printf("  channel %11d: enable %d disable %d\n", causes[i], enabled,
		       disabled);
	}
}

// How many of each object the kernel will hand out before it refuses.
static void testLimits() {
	static const int wanted = 300;
	static s32 ids[300];

	int made = 0;
	for (; made < wanted; ++made) {
		ee_sema_t info;
		memset(&info, 0, sizeof(info));
		info.init_count = 0;
		info.max_count = 1;
		ids[made] = CreateSema(&info);
		if (ids[made] < 0) {
			break;
		}
	}
	printf("Semaphores before a refusal: %d, then %d\n", made,
	       made < wanted ? ids[made] : 0);
	for (int i = 0; i < made; ++i) {
		DeleteSema(ids[i]);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testThreadErrors();
	testStaleId();
	testCreatePriorities();
	testSemaphoreErrors();
	testAlarmErrors();
	testInterruptErrors();
	testLimits();
	testReferWithBadId();
	testCreateBadBuffers();

	printf("-- TEST END\n");
	return 0;
}
