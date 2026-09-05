#include <common-iop.h>
#include <sysclib.h>
#include <thbase.h>
#include "spu2regs.h"

// One voice playing a block whose sample values are known, sampled as it goes.
// Nothing here listens to the output: what is recorded is the envelope and the
// address the voice has reached, which is what a driver polls.

#define IOP_DMA4_MADR 0x1F8010C0
#define IOP_DMA4_BCR 0x1F8010C4
#define IOP_DMA4_CHCR 0x1F8010C8
#define IOP_DMA_PCR 0x1F8010F0

#define VOICE_VOLL 0x00
#define VOICE_VOLR 0x02
#define VOICE_PITCH 0x04
#define VOICE_ADSR1 0x06
#define VOICE_ADSR2 0x08
#define VOICE_ENVX 0x0A

#define VOICE_SSAH 0x1C0
#define VOICE_SSAL 0x1C2
#define VOICE_LSAXH 0x1C4
#define VOICE_LSAXL 0x1C6
#define VOICE_NAXH 0x1C8
#define VOICE_NAXL 0x1CA

static const u32 blockAddress = 0x5000;

// Two sixteen byte blocks: a header byte pair then fourteen bytes of packed
// four bit samples.  The second sets the end flag so the voice stops on its
// own rather than running until it is keyed off.
static u32 block[8] __attribute__((aligned(64)));

static u32 read32(u32 address) {
	return *(volatile u32 *)address;
}

static void write32(u32 address, u32 value) {
	*(volatile u32 *)address = value;
}

static u32 voiceOffset(int voice, u32 field) {
	return voice * 16 + field;
}

static u32 addressOffset(int voice, u32 field) {
	return field + voice * 12;
}

// Bit fifteen of the attribute register turns the core on.  Turning it off
// and on again is the closest thing to a reset a program can ask for.
static const u16 coreEnable = 0x8000;

static void restartCore(void) {
	coreWrite(SPU2_CORE0, SPU2_ATTR, 0);
	DelayThread(10000);
	coreWrite(SPU2_CORE0, SPU2_ATTR, coreEnable);
	DelayThread(10000);
}

static void buildBlock(u8 shiftAndFilter, u8 flags) {
	u8 *bytes = (u8 *)block;
	int i;

	memset(block, 0, sizeof(block));

	bytes[0] = shiftAndFilter;
	bytes[1] = 0;
	for (i = 2; i < 16; ++i) {
		bytes[i] = (u8)(0x11 * (i - 1));
	}

	bytes[16] = shiftAndFilter;
	bytes[17] = flags;
	for (i = 18; i < 32; ++i) {
		bytes[i] = 0;
	}
}

static int sendBlock(void) {
	int spins;

	write32(IOP_DMA_PCR, read32(IOP_DMA_PCR) | 0x000F0000);
	coreWrite(SPU2_CORE0, SPU2_ATTR, coreEnable | 0x0020);
	coreWrite(SPU2_CORE0, SPU2_TSAH, (u16)(blockAddress >> 16));
	coreWrite(SPU2_CORE0, SPU2_TSAL, (u16)blockAddress);

	write32(IOP_DMA4_MADR, (u32)block & 0x00FFFFFF);
	write32(IOP_DMA4_BCR, (1 << 16) | 8);
	write32(IOP_DMA4_CHCR, 0x01000201);

	for (spins = 0; spins < 200000; ++spins) {
		if ((read32(IOP_DMA4_CHCR) & 0x01000000) == 0) {
			coreWrite(SPU2_CORE0, SPU2_ATTR, 0);
			return spins;
		}
	}
	write32(IOP_DMA4_CHCR, 0);
	coreWrite(SPU2_CORE0, SPU2_ATTR, coreEnable);
	return -1;
}

static void setVoice(int voice, u16 pitch, u16 attack, u16 sustain) {
	coreWrite(SPU2_CORE0, voiceOffset(voice, VOICE_VOLL), 0x3FFF);
	coreWrite(SPU2_CORE0, voiceOffset(voice, VOICE_VOLR), 0x3FFF);
	coreWrite(SPU2_CORE0, voiceOffset(voice, VOICE_PITCH), pitch);
	coreWrite(SPU2_CORE0, voiceOffset(voice, VOICE_ADSR1), attack);
	coreWrite(SPU2_CORE0, voiceOffset(voice, VOICE_ADSR2), sustain);

	coreWrite(SPU2_CORE0, addressOffset(voice, VOICE_SSAH),
	          (u16)(blockAddress >> 16));
	coreWrite(SPU2_CORE0, addressOffset(voice, VOICE_SSAL), (u16)blockAddress);
	coreWrite(SPU2_CORE0, addressOffset(voice, VOICE_LSAXH),
	          (u16)(blockAddress >> 16));
	coreWrite(SPU2_CORE0, addressOffset(voice, VOICE_LSAXL), (u16)blockAddress);
}

static void keyOn(int voice) {
	coreWrite(SPU2_CORE0, SPU2_KON, (u16)(1u << voice));
	coreWrite(SPU2_CORE0, SPU2_KON + 2, 0);
}

static void keyOff(int voice) {
	coreWrite(SPU2_CORE0, SPU2_KOFF, (u16)(1u << voice));
	coreWrite(SPU2_CORE0, SPU2_KOFF + 2, 0);
}

static void printVoice(int voice, const char *what) {
	printf("  %-20s envx %04x nax %04x %04x endx %04x %04x stat %04x\n", what,
	       coreRead(SPU2_CORE0, voiceOffset(voice, VOICE_ENVX)),
	       coreRead(SPU2_CORE0, addressOffset(voice, VOICE_NAXH)),
	       coreRead(SPU2_CORE0, addressOffset(voice, VOICE_NAXL)),
	       coreRead(SPU2_CORE0, SPU2_ENDX), coreRead(SPU2_CORE0, SPU2_ENDX + 2),
	       coreRead(SPU2_CORE0, SPU2_STATX));
}

// The voice registers with nothing playing, which is where every trace starts.
static void testIdleVoice(void) {
	restartCore();
	printf("Voice zero with nothing playing:\n");
	printVoice(0, "idle");
}

// One block played through, sampled as it goes.
static void testPlayOnce(void) {
	int i;

	restartCore();
	buildBlock(0x0C, 0x01);
	printf("Playing one block, end flag set:\n");
	printf("  the block went in after %d checks\n", sendBlock());

	setVoice(0, 0x1000, 0x00FF, 0x00FF);
	printVoice(0, "before key on");

	coreWrite(SPU2_CORE0, SPU2_ENDX, 0xFFFF);
	coreWrite(SPU2_CORE0, SPU2_ENDX + 2, 0x00FF);
	keyOn(0);

	for (i = 0; i < 8; ++i) {
		char name[24];
		DelayThread(2000);
		sprintf(name, "after %2d ms", (i + 1) * 2);
		printVoice(0, name);
	}

	keyOff(0);
	DelayThread(4000);
	printVoice(0, "after key off");
}

// The envelope with each attack rate, which is the field a driver sets per
// sound.
static void testEnvelopeRates(void) {
	static const u16 rates[] = {0x0000, 0x000F, 0x00FF, 0x7FFF, 0x8000, 0xFFFF};
	unsigned i;

	restartCore();
	buildBlock(0x0C, 0x04);
	sendBlock();

	printf("The envelope with each attack setting:\n");
	for (i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
		char name[32];

		keyOff(0);
		DelayThread(2000);
		setVoice(0, 0x1000, rates[i], 0x00FF);
		keyOn(0);
		DelayThread(4000);

		sprintf(name, "attack %04x", rates[i]);
		printVoice(0, name);
	}
	keyOff(0);
}

// The pitch, which decides how fast the address moves.
static void testPitch(void) {
	static const u16 pitches[] = {0x0000, 0x0400, 0x1000, 0x2000, 0x3FFF, 0xFFFF};
	unsigned i;

	restartCore();
	buildBlock(0x0C, 0x04);
	sendBlock();

	printf("The address after four milliseconds at each pitch:\n");
	for (i = 0; i < sizeof(pitches) / sizeof(pitches[0]); ++i) {
		char name[32];

		keyOff(0);
		DelayThread(2000);
		setVoice(0, pitches[i], 0x00FF, 0x00FF);
		keyOn(0);
		DelayThread(4000);

		sprintf(name, "pitch %04x", pitches[i]);
		printVoice(0, name);
	}
	keyOff(0);
}

// The header byte, whose low nibble is the shift and high nibble the filter,
// taken across every value.
static void testHeaderByte(void) {
	u8 value;

	printf("The address reached for each header byte:\n");
	for (value = 0; value < 0x50; value += 0x11) {
		char name[32];

		restartCore();
		buildBlock(value, 0x04);
		sendBlock();
		setVoice(0, 0x1000, 0x00FF, 0x00FF);
		keyOn(0);
		DelayThread(4000);

		sprintf(name, "header %02x", value);
		printVoice(0, name);
		keyOff(0);
	}
}

int _start(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	testIdleVoice();
	testPlayOnce();
	testEnvelopeRates();
	testPitch();
	testHeaderByte();

	restartCore();

	printf("-- TEST END\n");
	return 1;
}
