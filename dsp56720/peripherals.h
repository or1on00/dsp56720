#pragma once

#include <dsp56kEmu/dsp.h>

namespace dsp56720 {
struct Address {
	dsp56k::EMemArea area;
	dsp56k::TWord value;
};

constexpr Address operator"" _xmem (unsigned long long address) {
    return Address{dsp56k::MemArea_X, dsp56k::TWord(address)};
}

constexpr Address operator"" _ymem (unsigned long long address) {
    return Address{dsp56k::MemArea_Y, dsp56k::TWord(address)};
}

struct Register {
	using Read = std::function<dsp56k::TWord(dsp56k::Instruction)>;
	using Write = std::function<void(dsp56k::TWord)>;

	std::string name;
	Address address;
	Read read;
	Write write;
};

class Peripheral {
public:
	virtual void exec() = 0;
	virtual void reset() = 0;
	virtual void terminate() = 0;
	virtual std::vector<Register> registers() = 0;
	void connect(dsp56k::DSP& dsp) { m_dsp = &dsp; }

protected:
	void interrupt(uint32_t n) { m_dsp->injectInterrupt(n); }
	const uint32_t getInstructionCounter() const { return m_dsp->getInstructionCounter(); }

private:
	dsp56k::DSP* m_dsp;
};

// Peripheral with a single register
template <typename T>
class SimplePeripheral : public Peripheral {
public:
	virtual void exec() override {}
	virtual void reset() override {}
	virtual void terminate() override {}
	virtual std::vector<Register> registers() override { return m_registers; }

	virtual dsp56k::TWord read() = 0;
	virtual void write(dsp56k::TWord value) = 0;

private:
	std::vector<Register> m_registers = {
		{T::Name, T::Addr,
			[&](auto inst) { return read(); },
			[&](auto value) { write(value); }}
	};
};

class Peripherals;

class Peripherals : public dsp56k::IPeripherals {
public:
	Peripherals(std::initializer_list<std::reference_wrapper<Peripheral>> list);
	void add(Peripheral& peripheral);

	dsp56k::TWord read(dsp56k::EMemArea area, dsp56k::TWord addr, dsp56k::Instruction inst);
	void write(dsp56k::EMemArea area, dsp56k::TWord addr, dsp56k::TWord val);
	void exec();
	void reset();
	void terminate();
	void setSymbols(dsp56k::Disassembler& _disasm);

private:
	static const size_t size = dsp56k::XIO_Reserved_High_Last
		- dsp56k::XIO_Reserved_High_First + 1;

	Register* findRegister(dsp56k::EMemArea area, dsp56k::TWord addr);

	std::vector<std::reference_wrapper<Peripheral>> m_peripherals;
	std::unordered_map<dsp56k::TWord, Register> m_x, m_y;
	StaticArray<dsp56k::TWord, size * 2> m_mem;
};
}
