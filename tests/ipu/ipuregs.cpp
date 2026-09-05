#include "ipuregs.h"

namespace IPU {

bool reset() {
	*CTRL = CTRL_RST;
	for (int i = 0; i < 100000; ++i) {
		if ((*CTRL & (CTRL_RST | CTRL_BUSY)) == 0) {
			return true;
		}
	}
	return false;
}

void write(u32 command) {
	*CMD = command;
}

bool waitForIdle() {
	for (int i = 0; i < 100000; ++i) {
		if (!isBusy()) {
			return true;
		}
	}
	return false;
}

}
