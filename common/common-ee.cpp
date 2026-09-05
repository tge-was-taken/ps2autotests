#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <kernel.h>

//Kernel tests will add and substract values as big as 0x20 to test thread priority, 
//so, make sure this constant will keep the priority values between [0x01, 0x7F]
#define TEST_THREAD_PRIORITY   0x40

#define TEST_THREAD_STACK_SIZE 0x8000

char schedfBuffer[65536];
unsigned int schedfBufferPos = 0;

static int outputFd = -1;

static void emit(const char *text) {
	printf("%s", text);
	if (outputFd >= 0) {
		write(outputFd, text, strlen(text));
	}
}

void testPrintf(const char *format, ...) {
	static char line[16384];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	emit(line);
}

void schedf(const char *format, ...) {
	va_list args;
	va_start(args, format);
	schedfBufferPos += vsprintf(schedfBuffer + schedfBufferPos, format, args);
	va_end(args);
}

void flushschedf() {
	emit(schedfBuffer);
	schedfBuffer[0] = '\0';
	schedfBufferPos = 0;
}

int getThreadPriority(int threadId) {
	ee_thread_status_t threadStat;
	memset(&threadStat, 0, sizeof(ee_thread_status_t));
	int result = ReferThreadStatus(threadId, &threadStat);
	if (result >= 0) {
		return threadStat.current_priority;
	} else {
		return -1;
	}
}

u32 g_testThreadDoneSema = 0;

extern int test_main(int argc, char *argv[]);

char testThreadStack[TEST_THREAD_STACK_SIZE] __attribute__ ((aligned(16)));
void testThreadProc() {
	//TODO: Print begin/end traces
	//TODO: Pass arguments from main thread to the test function
	test_main(0, NULL);
	SignalSema(g_testThreadDoneSema);
}

// A console without a host to print to still has somewhere to put the output.
// TEST_OUTPUT_FILE names it at build time, -o<path> on the command line wins.
static const char *outputPath(int argc, char *argv[]) {
#ifdef TEST_OUTPUT_FILE
	const char *path = TEST_OUTPUT_FILE;
#else
	const char *path = NULL;
#endif
	for (int i = 1; i < argc; ++i) {
		if (strncmp(argv[i], "-o", 2) == 0 && argv[i][2] != '\0') {
			path = argv[i] + 2;
		}
	}
	return path;
}

int main(int argc, char *argv[]) {
	//A thread is created to ensure the execution environment is the same
	//across all possible boot methods (direct hw boot, ps2link, emulator, etc.)
	
	const char *path = outputPath(argc, argv);
	if (path != NULL) {
		outputFd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	}
	
	ee_sema_t semaInfo;
	memset(&semaInfo, 0, sizeof(ee_sema_t));
	semaInfo.init_count = 0;
	semaInfo.max_count  = 1;
	semaInfo.option     = 0;
	g_testThreadDoneSema = CreateSema(&semaInfo);
	
	u32 testThreadId = 0;
	ee_thread_t threadParam;
	memset(&threadParam, 0, sizeof(ee_thread_t));
	threadParam.func               = (void*)&testThreadProc;
	threadParam.initial_priority   = TEST_THREAD_PRIORITY;
	threadParam.stack_size         = TEST_THREAD_STACK_SIZE;
	threadParam.stack              = testThreadStack;
	testThreadId = CreateThread(&threadParam);
	
	StartThread(testThreadId, NULL);
	WaitSema(g_testThreadDoneSema);
	
	if (outputFd >= 0) {
		close(outputFd);
		outputFd = -1;
	}
	
	return 0;
}
