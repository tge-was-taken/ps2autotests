#ifndef PS2AUTOTESTS_GIF_GIFPROBE_H
#define PS2AUTOTESTS_GIF_GIFPROBE_H

#include <common-ee.h>
#include "../gs/emit_giftag.h"

// Nothing reports back how many quadwords a giftag consumed, so a second tag
// goes after the data and writes LABEL: SIGLBLID says whether it was reached.
namespace GIF {

	static const u32 probeValue = 0x0000ABCD;
	static const u64 noLabel = 0x1111111111111111ULL;

	// A packed tag that writes probeValue to LABEL and ends the packet.
	void WriteProbe(Packet &packet);

	// Sends down path 3, and returns what LABEL ended up holding.
	u64 SendAndReadLabel(Packet &packet);

	// LABEL lives in the upper half of SIGLBLID, SIGNAL in the lower.
	bool ProbeReached(u64 label);

	// Empties the unit, so a packet that ended mid tag cannot be mistaken for
	// the start of the next test's.
	void ResetUnit();
}

#endif
