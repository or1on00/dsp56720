#pragma once

#include <dsp56kEmu/dsp.h>
#include "peripherals.h"

namespace dsp56720 {
class ChipIdentification : public SimplePeripheral<ChipIdentification> {
public:
	static constexpr const char* Name = "CHIDR";
	static constexpr Address Addr = 0xFFFFF5_xmem;

	ChipIdentification(size_t core) : m_core(core) {}

	virtual dsp56k::TWord read() override { return 0x720 | (m_core << 16); }
	virtual void write(dsp56k::TWord write) override {}

private:
	size_t m_core;
};
}
