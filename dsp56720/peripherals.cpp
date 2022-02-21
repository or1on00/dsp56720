#include "peripherals.h"
#include "dsp56kEmu/aar.h"
#include "dsp56kEmu/esai.h"

namespace dsp56720 {
Peripherals::Peripherals(std::initializer_list<std::reference_wrapper<Peripheral>> list) {
	for (auto& peripheral : list) {
		add(peripheral);
	}
}

void Peripherals::add(Peripheral& peripheral) {
	m_peripherals.push_back(peripheral);

	for (auto& reg : peripheral.registers()) {
		switch (reg.address.area) {
		case dsp56k::MemArea_X:
			m_x.emplace(reg.address.value, reg);
			break;
		case dsp56k::MemArea_Y:
			m_y.emplace(reg.address.value, reg);
			break;
		}
	}
}

Register *findRegisterFrom(std::unordered_map<dsp56k::TWord, Register>& m, dsp56k::TWord addr) {
	auto it = m.find(addr);
	if (it != m.end()) {
		return &it->second;
	}

	return nullptr;
}

Register* Peripherals::findRegister(dsp56k::EMemArea area, dsp56k::TWord addr) {
	switch (area) {
		case dsp56k::MemArea_X:
			return findRegisterFrom(m_x, addr);
		case dsp56k::MemArea_Y:
			return findRegisterFrom(m_y, addr);
	}

	return nullptr;
}

size_t bank(dsp56k::EMemArea area) {
	switch (area) {
		case dsp56k::MemArea_X:
			return 0;
		case dsp56k::MemArea_Y:
			return 1;
		default:
			return 2;
	}
}

const char *areaName(dsp56k::EMemArea area) {
	switch (area) {
		case dsp56k::MemArea_P:
			return "(P)";
		case dsp56k::MemArea_X:
			return "(X)";
		case dsp56k::MemArea_Y:
			return "(Y)";
		default:
			return "(unknown)";
	}
}

dsp56k::TWord Peripherals::read(dsp56k::EMemArea area, dsp56k::TWord addr, dsp56k::Instruction inst) {
	auto reg = findRegister(area, addr);
	if (reg) {
		return reg->read(inst);
	}

	auto offset = addr - dsp56k::XIO_Reserved_High_First;
	auto index = offset * bank(area);

	auto pc = getDSP().getPC().toWord();
	if (index >= m_mem.size()) {
		LOG("Periph " << areaName(area)
				<< " out-of-bounds read $" << HEX(addr) << ": returning 0"
				<< " at " << HEX(pc));
		return 0;
	}

	auto& value = m_mem[index];
	LOG("Periph " << areaName(area) << " read $" << HEX(addr)
			<< ": returning 0x" <<  HEX(value)
			<< " at " << HEX(pc));

	return value;
}

void Peripherals::write(dsp56k::EMemArea area, dsp56k::TWord addr, dsp56k::TWord val) {
	auto reg = findRegister(area, addr);
	if (reg) {
		reg->write(val);
		return;
	}

	auto offset = addr - dsp56k::XIO_Reserved_High_First;
	auto index = offset * bank(area);

	auto pc = getDSP().getPC().toWord();
	if (index >= m_mem.size()) {
		LOG("Periph " << areaName(area) << " ignored out-of-bounds write $" << HEX(addr)
			<< " at " << HEX(pc));
		return;
	}

	LOG("Periph " << areaName(area) << " write $" << HEX(addr) << ": 0x" << HEX(val)
		<< " at " << HEX(pc));
	m_mem[index] = val;
}

void Peripherals::exec() {
	for (auto& peripheral : m_peripherals) {
		peripheral.get().exec();
	}
}

void Peripherals::reset() {
	for (auto& peripheral : m_peripherals) {
		peripheral.get().connect(getDSP());
		peripheral.get().reset();
	}
}

void Peripherals::terminate() {
	for (auto& peripheral : m_peripherals) {
		peripheral.get().terminate();
	}
}

void Peripherals::setSymbols(dsp56k::Disassembler& disasm) {
	for (auto& pair : m_x) {
		auto& reg = pair.second;
		disasm.addSymbol(dsp56k::Disassembler::MemX, reg.address.value, reg.name);
	}

	for (auto& pair : m_y) {
		auto& reg = pair.second;
		disasm.addSymbol(dsp56k::Disassembler::MemY, reg.address.value, reg.name);
	}
}
}
