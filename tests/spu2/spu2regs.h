#ifndef PS2AUTOTESTS_SPU2_SPU2REGS_H
#define PS2AUTOTESTS_SPU2_SPU2REGS_H

#include <common-iop.h>

#define SPU2_BASE 0x1F900000
#define SPU2_END 0x1F900800

#define SPU2_CORE0 0x1F900000
#define SPU2_CORE1 0x1F900400

#define SPU2_ATTR 0x19A
#define SPU2_KON 0x1A0
#define SPU2_KOFF 0x1A4
#define SPU2_TSAH 0x1A8
#define SPU2_TSAL 0x1AA
#define SPU2_DATA 0x1AC
#define SPU2_ADMAS 0x1B0
#define SPU2_ENDX 0x340
#define SPU2_STATX 0x344

static inline u16 spu2Read(u32 address) {
	return *(volatile u16 *)address;
}

static inline void spu2Write(u32 address, u16 value) {
	*(volatile u16 *)address = value;
}

static inline u16 coreRead(u32 core, u32 offset) {
	return spu2Read(core + offset);
}

static inline void coreWrite(u32 core, u32 offset, u16 value) {
	spu2Write(core + offset, value);
}

#endif
