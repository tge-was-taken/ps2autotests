#include <common-ee.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include "../dmaregs.h"
#include "../dmasend.h"
#include "../dmatags.h"

// Every source chain tag id, read back through where the transfer left MADR,
// TADR and QWC.  The scratchpad channel is the destination because it needs no
// other unit to be in a particular state.

static volatile DMA::Channel *const toSPR = DMA::D9;

static const u32 sprBytes = 16 * 1024;
static const u32 markerBase = 0xC0DE0000;

static u32 *source = 0;

static void fillSource() {
	for (u32 i = 0; i < sprBytes / 4; ++i) {
		source[i] = markerBase + i;
	}
	SyncDCache(source, (u8 *)source + sprBytes);
}

static void clearScratchpad() {
	volatile u32 *scratch = (volatile u32 *)0x70000000;
	for (u32 i = 0; i < sprBytes / 4; ++i) {
		scratch[i] = 0;
	}
}

static u32 scratchpadWord(u32 index) {
	return ((volatile u32 *)0x70000000)[index];
}

struct Outcome {
	u32 madr;
	u32 tadr;
	u32 qwc;
	u32 chcr;
	u32 stat;
};

static void sendChain(void *tag, Outcome &outcome) {
	*DMA::D_CTRL = DMA::D_CTRL_DMAE;
	*DMA::D_STAT = (DMA::RegSTATBits)0x0000FFFF;

	toSPR->sadr = 0;
	toSPR->madr = 0;
	toSPR->qwc = 0;
	toSPR->tadr = tag;
	toSPR->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_MOD_CHAIN | DMA::CHCR_STR);

	int spins = 1000000;
	while (--spins > 0 && toSPR->chcr.Ongoing()) {
		continue;
	}

	outcome.madr = (u32)toSPR->madr;
	outcome.tadr = (u32)toSPR->tadr;
	outcome.qwc = toSPR->qwc;
	outcome.chcr = toSPR->chcr.bits_;
	outcome.stat = DMA::D_STAT->bits_;
	toSPR->chcr = (DMA::ChannelRegCHCRBits)0;
}

static void printOutcome(const char *name, const Outcome &outcome) {
	printf("  %-22s madr %08x tadr %08x qwc %d chcr %08x stat %08x\n", name,
	       outcome.madr, outcome.tadr, outcome.qwc, outcome.chcr, outcome.stat);
	printf("    scratchpad: %08x %08x %08x %08x %08x\n", scratchpadWord(0),
	       scratchpadWord(4), scratchpadWord(8), scratchpadWord(12),
	       scratchpadWord(16));
}

// A single tag of each kind, so what each one transfers and where it leaves
// the pointers is separated from any chaining.  Where the next tag sits
// depends on the kind: cnt and next carry their data behind the tag, the ref
// family names it elsewhere and is followed straight away.
static void testSingleTags() {
	printf("One tag then an end:\n");

	static const DMA::SrcChainType ids[] = {
		DMA::SRC_REFE, DMA::SRC_CNT, DMA::SRC_NEXT, DMA::SRC_REF,
		DMA::SRC_REFS, DMA::SRC_END,
	};
	static const char *const names[] = {"refe", "cnt", "next", "ref", "refs", "end"};

	for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
		fillSource();
		clearScratchpad();

		DMA::SrcChainTag *tags = (DMA::SrcChainTag *)memalign(16, 16 * 8);
		memset(tags, 0, 16 * 8);

		const bool inlineData = ids[i] == DMA::SRC_CNT || ids[i] == DMA::SRC_NEXT ||
		                        ids[i] == DMA::SRC_END;
		tags[0].QWC = 2;
		tags[0].ID = ids[i];
		tags[0].IRQ = 0;
		tags[0].addr = inlineData ? (void *)0 : source;

		if (inlineData) {
			((u32 *)&tags[1])[0] = markerBase + 0x1000;
			((u32 *)&tags[2])[0] = markerBase + 0x2000;
		}
		// next names its successor; the others fall into the tag that follows
		// whatever data they carried.
		const int successor = inlineData ? 3 : 1;
		if (ids[i] == DMA::SRC_NEXT) {
			tags[0].addr = &tags[successor];
		}
		tags[successor].QWC = 0;
		tags[successor].ID = DMA::SRC_END;
		tags[successor].addr = 0;
		SyncDCache(tags, (u8 *)tags + 16 * 8);

		Outcome outcome;
		sendChain(tags, outcome);
		printOutcome(names[i], outcome);
		free(tags);
	}
}

// call and ret use a two entry stack, so this says what a third call does.
static void testCallStack() {
	printf("call and ret nesting:\n");

	for (int depth = 1; depth <= 3; ++depth) {
		fillSource();
		clearScratchpad();

		DMA::SrcChainTag *tags = (DMA::SrcChainTag *)memalign(16, 16 * 32);
		memset(tags, 0, 16 * 32);

		// Each level calls the next, and the innermost returns all the way.
		int at = 0;
		for (int level = 0; level < depth; ++level) {
			tags[at].QWC = 1;
			tags[at].ID = DMA::SRC_CALL;
			tags[at].addr = &tags[at + 4];
			((u32 *)&tags[at + 1])[0] = markerBase + 0x100 * (level + 1);
			at += 4;
		}
		for (int level = 0; level < depth; ++level) {
			tags[at].QWC = 1;
			tags[at].ID = DMA::SRC_RET;
			tags[at].addr = 0;
			((u32 *)&tags[at + 1])[0] = markerBase + 0x200 * (level + 1);
			at += 2;
		}
		tags[at].QWC = 0;
		tags[at].ID = DMA::SRC_END;
		SyncDCache(tags, (u8 *)tags + 16 * 32);

		Outcome outcome;
		sendChain(tags, outcome);

		char name[32];
		sprintf(name, "depth %d", depth);
		printOutcome(name, outcome);
		free(tags);
	}
}

// A tag with no data at all, which the counter has to handle without running
// off into whatever follows.
static void testZeroLength() {
	printf("Zero length tags:\n");

	static const DMA::SrcChainType ids[] = {DMA::SRC_CNT, DMA::SRC_REF, DMA::SRC_REFE};
	static const char *const names[] = {"cnt qwc 0", "ref qwc 0", "refe qwc 0"};

	for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
		fillSource();
		clearScratchpad();

		DMA::SrcChainTag *tags = (DMA::SrcChainTag *)memalign(16, 16 * 8);
		memset(tags, 0, 16 * 8);
		tags[0].QWC = 0;
		tags[0].ID = ids[i];
		tags[0].addr = source;
		tags[1].QWC = 1;
		tags[1].ID = DMA::SRC_CNT;
		tags[1].addr = 0;
		((u32 *)&tags[2])[0] = markerBase + 0x3000;
		tags[3].QWC = 0;
		tags[3].ID = DMA::SRC_END;
		tags[3].addr = 0;
		SyncDCache(tags, (u8 *)tags + 16 * 8);

		Outcome outcome;
		sendChain(tags, outcome);
		printOutcome(names[i], outcome);
		free(tags);
	}
}

// The interrupt bit on a tag, against the channel mask that decides whether
// the transfer stops for it.
static void testTagInterrupt() {
	printf("The tag interrupt bit:\n");

	for (int enabled = 0; enabled < 2; ++enabled) {
		fillSource();
		clearScratchpad();

		DMA::SrcChainTag *tags = (DMA::SrcChainTag *)memalign(16, 16 * 8);
		memset(tags, 0, 16 * 8);
		tags[0].QWC = 1;
		tags[0].ID = DMA::SRC_CNT;
		tags[0].IRQ = 1;
		((u32 *)&tags[1])[0] = markerBase + 0x4000;
		tags[2].QWC = 1;
		tags[2].ID = DMA::SRC_CNT;
		((u32 *)&tags[3])[0] = markerBase + 0x5000;
		tags[4].QWC = 0;
		tags[4].ID = DMA::SRC_END;
		SyncDCache(tags, (u8 *)tags + 16 * 8);

		*DMA::D_CTRL = DMA::D_CTRL_DMAE;
		*DMA::D_STAT = (DMA::RegSTATBits)0x0000FFFF;
		toSPR->sadr = 0;
		toSPR->madr = 0;
		toSPR->qwc = 0;
		toSPR->tadr = tags;
		toSPR->chcr = (DMA::ChannelRegCHCRBits)(DMA::CHCR_MOD_CHAIN | DMA::CHCR_STR |
		                                        (enabled ? DMA::CHCR_TIE : 0));

		int spins = 1000000;
		while (--spins > 0 && toSPR->chcr.Ongoing()) {
			continue;
		}

		Outcome outcome;
		outcome.madr = (u32)toSPR->madr;
		outcome.tadr = (u32)toSPR->tadr;
		outcome.qwc = toSPR->qwc;
		outcome.chcr = toSPR->chcr.bits_;
		outcome.stat = DMA::D_STAT->bits_;
		toSPR->chcr = (DMA::ChannelRegCHCRBits)0;

		printOutcome(enabled ? "TIE set" : "TIE clear", outcome);
		free(tags);
	}
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	source = (u32 *)memalign(16, sprBytes);

	testSingleTags();
	testCallStack();
	testZeroLength();
	testTagInterrupt();

	free(source);

	printf("-- TEST END\n");
	return 0;
}
