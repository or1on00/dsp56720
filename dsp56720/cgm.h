#pragma once

#include <dsp56kEmu/dsp.h>
#include "peripherals.h"
#include "bitfield.h"

namespace dsp56720 {
class ClockGenerationModule : public Peripheral {
public:
	virtual void exec() override { }
	virtual void reset() override {}
	virtual void terminate() override {}
	virtual std::vector<Register> registers() override { return m_registers; }

	uint32_t cyclesPerSample() { return m_cyclesPerSample; }

	struct PCTL : BitField<dsp56k::TWord> {
		using BitField<dsp56k::TWord>::operator=;

		using F = Packed<0, 8>;   // Multiplication Factor
		using R = Packed<16, 5>;  // Input Divider
		using OD = Packed<14, 2>; // Output Divider
	};

	void updatePCTL(dsp56k::TWord _val) {
		// TODO:
	}

private:
	// estimate cycles per sample.
	uint32_t m_cyclesPerSample = 2133;

	std::vector<Register> m_registers = {
		{"PCTL",
		0xFFFF7D_xmem,
		[&](auto inst) { return 0; },
		[&](auto value) { updatePCTL(value); }}
	};
};
}
