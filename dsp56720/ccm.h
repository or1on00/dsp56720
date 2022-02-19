#pragma once

#include <dsp56kEmu/dsp.h>
#include "peripherals.h"
#include "bitfield.h"

namespace dsp56720 {
class ChipConfigurationModule : public Peripheral {
	virtual void exec() override {}
	virtual void reset() override {}
	virtual void terminate() override {}
	virtual std::vector<Register> registers() override { return m_registers; }

	dsp56k::TWord readExternalMemoryBurstControl() { return m_embc; }
	void writeExternalMemoryBurstControl(dsp56k::TWord value) { m_embc = value; }
	dsp56k::TWord readDebugAndBurstControl() { return 0; }
	void writeDebugAndBurstControl(dsp56k::TWord value) { }

private:
	dsp56k::TWord m_embc;

	std::vector<Register> m_registers = {
		// TODO: See Chapter 18 - EMC Burst Buffer in the DSP56720 reference manual
		{"EMBC", 0xFFFFE6_ymem,
			[&](auto inst) { return readExternalMemoryBurstControl(); },
			[&](auto value) { writeExternalMemoryBurstControl(value); }},
		{"ODBC", 0xFFFFE2_ymem,
			[&](auto inst) { return readDebugAndBurstControl(); },
			[&](auto value) { writeDebugAndBurstControl(value); }}
	};
};
}
