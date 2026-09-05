#ifndef PS2AUTOTESTS_IPU_IPUREGS_H
#define PS2AUTOTESTS_IPU_IPUREGS_H

#include <common-ee.h>

namespace IPU {

	static volatile u64 *const CMD = (volatile u64 *)0x10002000;
	static volatile u32 *const CTRL = (volatile u32 *)0x10002010;
	static volatile u32 *const BP = (volatile u32 *)0x10002020;
	static volatile u32 *const TOP = (volatile u32 *)0x10002030;

	static volatile u128 *const IN_FIFO = (volatile u128 *)0x10007000;
	static volatile u128 *const OUT_FIFO = (volatile u128 *)0x10007010;

	enum Command {
		CMD_BCLR = 0,
		CMD_IDEC = 1,
		CMD_BDEC = 2,
		CMD_VDEC = 3,
		CMD_FDEC = 4,
		CMD_SETIQ = 5,
		CMD_SETVQ = 6,
		CMD_CSC = 7,
		CMD_PACK = 8,
		CMD_SETTH = 9,
	};

	enum CtrlBits {
		CTRL_RST = 1u << 30,
		CTRL_BUSY = 1u << 31,
	};

	inline u32 inputCount() { return *CTRL & 0xF; }
	inline u32 outputCount() { return (*CTRL >> 4) & 0xF; }
	inline bool isBusy() { return (*CTRL & CTRL_BUSY) != 0; }

	// Returns false when the unit is still busy afterwards.  A write to the
	// command register while it is busy blocks the bus, so a caller has to check
	// this before sending the next one.
	bool reset();
	void write(u32 command);
	// Returns false when the unit was still busy after the wait, so a command
	// that waits for data it will never get does not hang the test.
	bool waitForIdle();

}

#endif
