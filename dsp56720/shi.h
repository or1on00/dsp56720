#pragma once

#include <cstdio>
#include <dsp56kEmu/dsp.h>
#include <dsp56kEmu/interrupts.h>
#include "peripherals.h"
#include "bitfield.h"
#include "queue.h"

namespace dsp56720 {
class SerialHostInterace : public Peripheral {
public:
	struct HCSR : BitField<dsp56k::TWord> {
		using BitField<dsp56k::TWord>::operator=;

		using HEN = Bit<0>;
		using HI2C = Bit<1>;
		using HM = Packed<2, 2>;
		using HCKFR = Bit<4>;
		using HFIFO = Bit<5>;
		using HMST = Bit<6>;
		using HRQE = Packed<7, 2>;
		using HIDLE = Bit<9>;
		using HBIE = Bit<10>;
		using HTIE = Bit<11>;
		using HRIE = Packed<12, 2>;
		using HTUE = Bit<14>;
		using HTDE = Bit<15>;
		using HRNE = Bit<17>;
		using HRFF = Bit<19>;
		using HROE = Bit<20>;
		using HBER = Bit<21>;
		using HBUSY = Bit<22>;
	};

	virtual void exec() override {
		if (!HCSR::HEN(m_hcsr)) {
			return;
		}


		if (m_pendingRXInterrupts > 0 && HCSR::HRIE(m_hcsr)) {
			--m_pendingRXInterrupts;

			// TODO: Document constant values
			// TODO: We probably want to move these checks into
			// where the interrupt is incremented
			switch (HCSR::HRIE(m_hcsr)) {
				case 1:
					if (!m_rx.empty()) {
						interrupt(dsp56k::Vba_SHI_Receive_FIFO_Not_Empty);
					}
					break;
				case 3:
					if (m_rx.full()) {
						interrupt(dsp56k::Vba_SHI_Receive_FIFO_Full);
					}
					break;
			}
		} else if (HCSR::HTIE(m_hcsr) && m_pendingTXInterrupts > 0) {
			--m_pendingTXInterrupts;
			interrupt(dsp56k::Vba_SHI_Transmit_Data);
		}
	}

	virtual void reset() override {}

	virtual void terminate() override {
		m_rx.shutdown();
		m_tx.shutdown();
	}

	dsp56k::TWord readStatusControlRegister(dsp56k::Instruction inst) {
		m_hcsr |= HCSR::HRNE(!m_rx.empty());
		return m_hcsr;
	}

	void writeStatusControlRegister(dsp56k::TWord value) {
		m_hcsr = value;
	}

	virtual std::vector<Register> registers() override {
		return m_registers;
	}

	void writeRX(const std::vector<dsp56k::TWord>& _data) {
		writeRX(&_data[0], _data.size());
	}

	void writeRX(const dsp56k::TWord* data, size_t count) {
		for (size_t i = 0; i < count; ++i) {
			m_rx.push(data[i] & 0x00ffffff);
			if (HCSR::HEN(m_hcsr) && HCSR::HRIE(m_hcsr)) {
				++m_pendingRXInterrupts;
			}
		}
	}

	void pipe(std::FILE *f) {
		for (;;) {
			uint8_t b[4];
			std::fread(b, sizeof(uint8_t), sizeof(b) / sizeof(uint8_t), f);
			if (std::feof(f)) {
				break;
			}

			uint32_t word = (b[0]<<0) | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);

			m_rx.push(word);
			if (HCSR::HEN(m_hcsr) && HCSR::HRIE(m_hcsr)) {
				++m_pendingRXInterrupts;
			}
		}
	}

        dsp56k::TWord readRX() {
		return m_rx.pop();
	}

        dsp56k::TWord readRX(dsp56k::Instruction _inst) {
                if (m_rx.empty()) {
                        LOG("Empty read");
                        return 0;
                }

                dsp56k::TWord res;

                switch (_inst) {
                case dsp56k::Btst_pp:
                case dsp56k::Btst_D:
                case dsp56k::Btst_qq:
                case dsp56k::Btst_ea:
                case dsp56k::Btst_aa:
                        res = m_rx.front();
                        break;
                default:
                        res = m_rx.pop();
                        break;
                }

                return res;
	}

	void writeTX(dsp56k::TWord value) {
		m_tx.push(value);
		++m_pendingTXInterrupts;
	}

	uint32_t readTX() {
		return m_tx.pop();
	}

private:
	HCSR m_hcsr;
	Queue<dsp56k::TWord, CircularBuffer<dsp56k::TWord, 8192>> m_rx;
	Queue<dsp56k::TWord, CircularBuffer<dsp56k::TWord, 8192>> m_tx;
	std::atomic<uint32_t> m_pendingRXInterrupts;
	std::atomic<uint32_t> m_pendingTXInterrupts;

	std::vector<Register> m_registers = {
		// SHI Receive FIFO
		{"HRX", 0xFFFF94_xmem,
			[&](auto inst) { return readRX(inst); },
			[&](auto value) {}},

		// SHI Transmit Register
		{"HTX", 0xFFFF93_xmem,
			[&](auto inst) { return 0; },
			[&](auto value) { writeTX(value); }},

		// SHI I2C Slave Address Register
		{"HSAR", 0xFFFF92_xmem,
			[&](auto inst) { return 0; },
			[&](auto value) {}},

		// SHI Control/Status Register
		{"HCSR", 0xFFFF91_xmem,
			[&](auto inst) { return readStatusControlRegister(inst); },
			[&](auto value) { writeStatusControlRegister(value); }},

		// SHI Clock Control Register
		{"HCKR", 0xFFFF90_xmem, [&](auto inst) { return 0; }, [&](auto value) {}},
	};
};
}
