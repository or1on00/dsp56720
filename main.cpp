#include <dsp56kEmu/dsp.h>
#include <cstring>
#include <csignal>

#include "dsp56720/peripherals.h"
#include "dsp56720/bitfield.h"
#include "dsp56720/shi.h"
#include "dsp56720/esai.h"
#include "dsp56720/cgm.h"
#include "dsp56720/ccm.h"
#include "dsp56720/chipid.h"
#include "vfs/filesystem.h"

#include <iostream>

class PinInterface : public vfs::File {
public:
	PinInterface(std::atomic<bool>& value) : m_value(value) {}

	virtual std::size_t size() {
		return 1;
	}

	virtual std::size_t read(char *buf, std::size_t count, std::size_t pos) override {
		if (pos > 0) {
			return 0;
		}

		if (!count) {
			return 0;
		}

		if (count > 1) {
			count = 1;
		}

		*buf = m_value + '0';
		return 1;
	}

	virtual std::size_t write(const char *buf, std::size_t count, std::size_t pos) override {
		if (pos > 0) {
			return 0;
		}

		if (!count) {
			return 0;
		}

		if (*buf == '0') {
			m_value = 0;
			return count;
		} else if (*buf == '1') {
			m_value = 1;
			return count;
		}

		return -EIO;
	}

private:
	std::atomic<bool>& m_value;
};

class SerialInterface : public vfs::File {
public:
	SerialInterface(dsp56720::SerialHostInterace& shi) : m_shi(shi) {}

	virtual std::size_t size() {
		return 4096;
	}

	virtual std::size_t read(char *buf, std::size_t count, std::size_t pos) override {
		return 0;
	}

	virtual std::size_t write(const char *buf, std::size_t count, std::size_t pos) override {
		assert(count % 4 == 0);
		size_t wordCount = count / sizeof(dsp56k::TWord);

		try {
			m_shi.writeRX(reinterpret_cast<const dsp56k::TWord*>(buf), wordCount);
		} catch(dsp56720::QueueShutdown&) {
			return 0;
		}

		return wordCount * sizeof(dsp56k::TWord);
	}

private:
	dsp56720::SerialHostInterace& m_shi;
};

class AudioInputInterface : public vfs::File {
public:
	AudioInputInterface(dsp56720::EnhancedSerialAudioInterface& esai, size_t n)
		: m_esai(esai), m_n(n) {}

	virtual std::size_t size() {
		return 4096;
	}

	virtual std::size_t read(char *buf, std::size_t count, std::size_t pos) override {
		return 0;
	}

	virtual std::size_t write(const char *buf, std::size_t count, std::size_t pos) override {
		// TODO: Allow different buffer sizes
		assert(count % 4 == 0);

		auto words = reinterpret_cast<const dsp56k::TWord*>(buf);

		try {
			for (size_t i = 0; i < count / sizeof(dsp56k::TWord); i++) {
				m_esai.writeInput(m_n, *words++);
			}
		} catch(dsp56720::QueueShutdown&) {
			return 0;
		}

		return count;
	}

private:
	dsp56720::EnhancedSerialAudioInterface& m_esai;
	size_t m_n;
};

class AudioOutputInterface : public vfs::File {
public:
	AudioOutputInterface(dsp56720::EnhancedSerialAudioInterface& esai, size_t n)
		: m_esai(esai), m_n(n) {}

	virtual std::size_t size() {
		return 4096;
	}

	virtual std::size_t read(char *buf, std::size_t count, std::size_t pos) override {
		// TODO: Allow different buffer sizes
		assert(count % 4 == 0);

		auto words = reinterpret_cast<dsp56k::TWord*>(buf);

		try {
			for (size_t i = 0; i < count / sizeof(dsp56k::TWord); i++) {
				*words++ = m_esai.readOutput(m_n);
			}
		} catch(dsp56720::QueueShutdown&) {
			return 0;
		}

		return count;
	}

	virtual std::size_t write(const char *buf, std::size_t count, std::size_t pos) override {
		return 0;
	}

private:
	dsp56720::EnhancedSerialAudioInterface& m_esai;
	size_t m_n;
};

std::function<void(int)> g_signalHandler;

void signalHandler(int signal) {
	if (g_signalHandler) {
		g_signalHandler(signal);
	}
}

int main(int argc, char *argv[]) {
	vfs::Filesystem fs("./mount");

	std::atomic<bool> running{true};
	std::atomic<bool> reset, moda0;

	// TODO: Use a EnumInterface mapping '0' -> 0 and '1' -> 1
	fs.tree().put("/pins/reset", PinInterface{reset});
	fs.tree().put("/pins/moda0", PinInterface{moda0});

	dsp56720::ClockGenerationModule cgm;
	dsp56720::ChipConfigurationModule ccm;
	dsp56720::SerialHostInterace shi0;
	dsp56720::EnhancedSerialAudioInterface esai{cgm};
	dsp56720::ChipIdentification chidr{0};

	fs.tree().put("/peripherals/shi0", SerialInterface{shi0});
	fs.tree().put("/peripherals/esai/input0", AudioInputInterface{esai, 0});
	fs.tree().put("/peripherals/esai/ouput0", AudioOutputInterface{esai, 0});
	fs.tree().put("/peripherals/esai/ouput1", AudioOutputInterface{esai, 1});
	fs.tree().put("/peripherals/esai/ouput2", AudioOutputInterface{esai, 2});

	dsp56720::Peripherals peripherals{cgm, ccm, shi0, esai, chidr};

	constexpr dsp56k::TWord g_memorySize = 0xf80000;
	const dsp56k::DefaultMemoryValidator memoryMap;
	dsp56k::Memory memory(memoryMap, g_memorySize);
	memory.setExternalMemory(0x020000, true);

	dsp56k::DSP dsp(memory, peripherals);

	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = signalHandler;
	sigemptyset(&(sa.sa_mask));
	sa.sa_flags = 0;

	if (sigaction(SIGINT, &sa, NULL) < 0) {
		return 1;
	}

	std::thread mainloop([&](){
		// TODO: Handle RX interrupt better
		try {
			auto count = shi0.readRX();
			auto address = shi0.readRX();

			std::cout << "Booting count=" << count << " words to address=" << address
				<< std::endl;

			for (size_t i = 0; i < count; i++) {
				auto word = shi0.readRX();
				dsp.memory().set(dsp56k::MemArea_P, address + i, word);
			}

			dsp.setPC(address);

			std::cout << "Booted" << std::endl;

			while (running) {
				/* if (halt) { */
				/* 	std::this_thread::yield(); */
				/* } else { */
					dsp.exec();
				/* } */
			}
		} catch(dsp56720::QueueShutdown&) {
			return;
		}
	});

	g_signalHandler = [&](auto signal) {
		std::cout << "INTERRUPTED!" << std::endl;

		running = false;
		dsp.terminate();

		fs.shutdown();
	};

	int ret = fs.run();
	mainloop.join();

	return ret;
}
