#include <common-ee.h>
#include <ee_regs.h>
#include "gifprobe.h"
#include "../dma/dmaregs.h"
#include "../dma/dmasend.h"
#include "../gs/gsregs.h"

namespace GIF {

void WriteProbe(Packet &packet) {
	Tag tag;
	tag.SetLoops(1);
	tag.SetEop();
	tag.SetFormat(FORMAT_PACKED);
	tag.SetRegDescs(REG_AD);

	packet.WriteTag(tag);
	packet.Emit(GS::LABEL(probeValue, 0xFFFFFFFF));
	packet.Emit(GS::REG_LABEL);
}

u64 SendAndReadLabel(Packet &packet) {
	*GS::SIGLBLID = noLabel;
	DMA::SendSimple(DMA::D2, packet.Raw(), packet.Size());
	const u64 label = *GS::SIGLBLID;
	// A signal the gs never sees acknowledged holds up the next packet.
	*GS::CSR = GS::CSR_SIGNAL;
	return label;
}

bool ProbeReached(u64 label) {
	return (u32)(label >> 32) == probeValue;
}

void ResetUnit() {
	*R_EE_GIF_CTRL = 1;
}

}
