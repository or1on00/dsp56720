#include <dsp56kEmu/dsp.h>

#include "dsp56720/peripherals.h"
#include "dsp56720/bitfield.h"
#include "dsp56720/shi.h"
#include "dsp56720/esai.h"
#include "dsp56720/cgm.h"
#include "dsp56720/ccm.h"
#include "dsp56720/chipid.h"

int main(int argc, char *argv[]) {
	dsp56720::ClockGenerationModule cgm;
	dsp56720::ChipConfigurationModule ccm;
	dsp56720::SerialHostInterace shi0;
	dsp56720::EnhancedSerialAudioInterface esai{cgm};
	dsp56720::ChipIdentification chidr{0};

	dsp56720::Peripherals peripherals{cgm, ccm, shi0, esai, chidr};

	constexpr dsp56k::TWord g_memorySize = 0xf80000;
	const dsp56k::DefaultMemoryValidator memoryMap;
	dsp56k::Memory memory(memoryMap, g_memorySize);
	memory.setExternalMemory(0x020000, true);

	dsp56k::DSP dsp(memory, peripherals);

	for (;;) {
		dsp.exec();
	}

	return 1;
}
