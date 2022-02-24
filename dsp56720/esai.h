#pragma once

#include <dsp56kEmu/dsp.h>
#include "peripherals.h"
#include "cgm.h"
#include "bitfield.h"

namespace dsp56720 {
class EnhancedSerialAudioInterface : public Peripheral {
public:
	struct SR : BitField<dsp56k::TWord> {
		using BitField<dsp56k::TWord>::operator=;

		using TFS = Bit<13>; // Transmit Frame Sync Flag
		using TUE = Bit<14>; // Transmit Underrun Error
		using TDE = Bit<15>; // Transmit Data Register Empty
	};

	struct RCR : BitField<dsp56k::TWord> {
		using BitField<dsp56k::TWord>::operator=;

		using RE = Set<0, 4>; // Receive Enable
	};

	struct TCR : BitField<dsp56k::TWord> {
		using BitField<dsp56k::TWord>::operator=;

		using TLIE = Bit<23>; // Transmit Last Slot Interrupt Enable
		using TIE = Bit<22>; // Transmit Interrupt Enable
		using TEIE = Bit<20>; // Transmit Exception Interrupt Enable
		using TE = Set<0, 6>; // Transmit Enable
	};

	EnhancedSerialAudioInterface(ClockGenerationModule& cgm) : m_cgm(cgm) {
		m_tx.fill(0);
		m_rx.fill(0);
	}

	virtual void exec() override {
		if(!TCR::TE(m_tcr)) {
			return;
		}

		const auto clock = getInstructionCounter();
		const auto diff = dsp56k::delta(clock, m_lastClock);
		m_lastClock = clock;

		m_cyclesSinceWrite += diff;
		if (m_cyclesSinceWrite <= m_cgm.cyclesPerSample()) {
			return;
		}

		// Time to xfer samples!
		m_cyclesSinceWrite -= m_cgm.cyclesPerSample();
		for (int i = 0; i < m_audioOutputs.size(); i++) {
			if (outputEnabled(i)) {
				m_audioOutputs[i].push(m_tx[i]);
			}
		}

		for (int i=0; i < m_audioInputs.size(); i++) {
			if (inputEnabled(i)) {
				m_rx[i] = m_audioInputs[i].pop();
			}
		}

		if (SR::TFS(m_sr)) {
			m_sr |= SR::TFS(0);
		} else {
			m_sr |= SR::TFS(1);
		}

		/* if (SR::TUE(m_sr) && TCR::TEIE(m_tcr)) { */
		/* 	interrupt(dsp56k::Vba_ESAI_Transmit_Data_with_Exception_Status); */
		/* } else if (TCR::TIE(m_tcr)) { */
		/* 	interrupt(dsp56k::Vba_ESAI_Transmit_Data); */
		/* } */

		if (TCR::TIE(m_tcr)) {
			interrupt(dsp56k::Vba_ESAI_Transmit_Data);
		}

		if (SR::TFS(m_sr) && TCR::TLIE(m_tcr)) {
			interrupt(dsp56k::Vba_ESAI_Transmit_Last_Slot);
		}

		m_sr |= SR::TUE(1);
		m_sr |= SR::TDE(1);
		m_writtenTX = 0;
		m_hasReadStatus = 0;
	}

	virtual void reset() override {}

	virtual void terminate() override {
		for (auto& input : m_audioInputs) {
			input.shutdown();
		}

		for (auto& output : m_audioOutputs) {
			output.shutdown();
		}
	}

	virtual std::vector<Register> registers() override {
		return m_registers;
	}

	void writeInput(size_t n, dsp56k::TWord word) {
		m_audioInputs[n].push(word);
	}

	dsp56k::TWord readOutput(size_t n) {
		return m_audioOutputs[n].pop();
	}

	dsp56k::TWord readRX(uint32_t index) {
		if (!inputEnabled(index)) {
			return 0;
		}

		return m_rx[index];
	}

	void writeTX(uint32_t index, dsp56k::TWord val) {
		if (!outputEnabled(index)) {
			return;
		}

		m_tx[index] = val;
		m_writtenTX |= (1 << index);

		if (m_writtenTX == TCR::TE(m_tcr)) {
			if (m_hasReadStatus) {
				m_sr |= SR::TUE(0);
			}

			m_sr |= SR::TDE(0);
		}
	}

	dsp56k::TWord readStatusRegister() {
		m_hasReadStatus = true;
		return m_sr;
	}

	void writestatusRegister(dsp56k::TWord val) {
		LOG("Write ESAI SR " << HEX(val));
		m_sr = val;
	}

	dsp56k::TWord readReceiveControlRegister() {
		return m_rcr;
	}

	dsp56k::TWord readTransmitControlRegister() {
		return m_tcr;
	}

	void writeReceiveControlRegister(dsp56k::TWord val) {
		LOG("Write ESAI RCR " << HEX(val) << " RE=" << RCR::RE(val));
		m_rcr = val;
	}

	void writeTransmitControlRegister(dsp56k::TWord val) {
		m_sr |= SR::TUE(0);
		LOG("Write ESAI TCR " << HEX(val) << " TE=" << TCR::TE(val));
		m_tcr = val;
	}

	void writeTransmitClockControlRegister(dsp56k::TWord val) {
		LOG("Write ESAI TCCR " << HEX(val));
		m_tccr = val;
	}

	dsp56k::TWord readControlRegister() {
		LOG("READ ESAI CR");
		return m_cr;
	}

	void writeControlRegister(dsp56k::TWord val) {
		LOG("Write ESAI CR " << HEX(val));
		m_cr = val;
	}

	void writeReceiveClockControlRegister(dsp56k::TWord val) {
		LOG("Write ESAI RCCR " << HEX(val));
		m_rccr = val;
	}

private:
	bool inputEnabled(uint32_t index) const {
		return RCR::RE(m_rcr).test(index);
	}

	bool outputEnabled(uint32_t index) const {
		return TCR::TE(m_tcr).test(index);
	}

	ClockGenerationModule& m_cgm;

	std::array<Queue<uint32_t, CircularBuffer<uint32_t, 8192>>, 4> m_audioInputs;
	std::array<Queue<uint32_t, CircularBuffer<uint32_t, 8192>>, 6> m_audioOutputs;

	// Words written by the DSP
	std::array<dsp56k::TWord, 6> m_tx;
	// Words for the DSP to read
	std::array<dsp56k::TWord, 6> m_rx;
	bool m_hasReadStatus = false; // Has the status register been read since TUE was set?
	uint32_t m_cyclesSinceWrite = 0;
	uint32_t m_writtenTX = 0;
	uint32_t m_lastClock = 0;

	SR m_sr;
	TCR m_tcr;
	RCR m_rcr;
	dsp56k::TWord m_rccr, m_cr, m_tccr;

	std::vector<Register> m_registers = {
		// ESAI Receive Data Register 3 (RX0)
		{"RX0", 0xFFFFA8_xmem,
		[&](auto inst) { return readRX(0); },
		[&](auto value) {}},

		// ESAI Receive Data Register 3 (RX1)
		{"RX1", 0xFFFFA9_xmem,
		[&](auto inst) { return readRX(1); },
		[&](auto value) {}},

		// ESAI Receive Data Register 3 (RX2)
		{"RX2", 0xFFFFAA_xmem,
		[&](auto inst) { return readRX(2); },
		[&](auto value) {}},

		// ESAI Receive Data Register 3 (RX3)
		{"RX3", 0xFFFFAB_xmem,
		[&](auto inst) { return readRX(3); },
		[&](auto value) {}},

		// ESAI Transmit Data Register 0 (TX0)
		{"TX0",
		0xFFFFA0_xmem,
		[&](auto inst) { return 0; },
		[&](auto value) { writeTX(0, value); }},

		// ESAI Transmit Data Register 1 (TX1)
		{"TX1",
		0xFFFFA1_xmem,
		[&](auto inst) { return 0; },
		[&](auto value) { writeTX(1, value); }},

		// ESAI Transmit Data Register 2 (TX2)
		{"TX2",
		0xFFFFA2_xmem,
		[&](auto inst) { return 0; },
		[&](auto value) { writeTX(2, value); }},

		// ESAI Transmit Data Register 3 (TX3)
		{"TX3",
		0xFFFFA3_xmem,
		[&](auto inst) { return 0; },
		[&](auto value) { writeTX(3, value); }},

		// ESAI Transmit Data Register 4 (TX4)
		{"TX4",
		0xFFFFA4_xmem,
		[&](auto inst) { return 0; },
		[&](auto value) { writeTX(4, value); }},

		// ESAI Transmit Data Register 5 (TX5)
		{"TX5",
		0xFFFFA5_xmem,
		[&](auto inst) { return 0; },
		[&](auto value) { writeTX(5, value); }},

		// ESAI Status Register (SAISR)
		{"SAISR",
		0xFFFFB3_xmem,
		[&](auto inst) { return readStatusRegister(); },
		[&](auto value) { writestatusRegister(value); }},

		// ESAI Control Register (SAICR)
		{"SAICR",
		0xFFFFB4_xmem,
		[&](auto inst) { return readControlRegister(); },
		[&](auto value) { writeControlRegister(value); }},

		// ESAI Receive Control Register (RCR)
		{"RCR",
		0xFFFFB7_xmem,
		[&](auto inst) { return readReceiveControlRegister(); },
		[&](auto value) { writeReceiveControlRegister(value); }},

		// ESAI Receive Clock Control Register (RCCR)
		{"RCCR",
		0xFFFFB8_xmem,
		[&](auto inst) { return 0; },
		[&](auto value) { writeReceiveClockControlRegister(value); }},

		// ESAI Transmit Control Register (TCR)
		{"TCR",
		0xFFFFB5_xmem,
		[&](auto inst) { return readTransmitControlRegister(); },
		[&](auto value) { writeTransmitControlRegister(value); }},

		// ESAI Transmit Clock Control Register (TCCR)
		{"TCCR",
		0xFFFFB6_xmem,
		[&](auto inst) { return 0; },
		[&](auto value) { writeTransmitClockControlRegister(value); }}
	};
};
}
