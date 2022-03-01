#include <dsp56kEmu/dsp.h>
#include <limits.h>
#include <cstring>
#include <csignal>
#include <cstdarg>
#include <iostream>

#include "dsp56720/peripherals.h"
#include "dsp56720/bitfield.h"
#include "dsp56720/debugger.h"
#include "dsp56720/shi.h"
#include "dsp56720/esai.h"
#include "dsp56720/cgm.h"
#include "dsp56720/ccm.h"
#include "dsp56720/chipid.h"
#include "vfs/filesystem.h"
#include "vfs/traits.h"

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

std::function<void(int)> g_signalHandler;

void signalHandler(int signal) {
	if (g_signalHandler) {
		g_signalHandler(signal);
	}
}

template <>
struct vfs::SequentialAccess<dsp56720::EnhancedSerialAudioInterface::Input> {
	static constexpr bool readable = false;
	static constexpr bool writable = true;

	void write(dsp56720::EnhancedSerialAudioInterface::Input& input, uint32_t sample) {
		try {
			input.writeSample(sample);
		} catch(dsp56720::QueueShutdown&) {
			throw vfs::Abort{};
		}
	}
};

template <>
struct vfs::SequentialAccess<dsp56720::EnhancedSerialAudioInterface::Output> {
	static constexpr bool readable = true;
	static constexpr bool writable = false;

	uint32_t read(dsp56720::EnhancedSerialAudioInterface::Output& output) {
		try {
			return output.readSample();
		} catch(dsp56720::QueueShutdown&) {
			throw vfs::Abort{};
		}
	}
};

template <>
struct vfs::SequentialAccess<dsp56720::SerialHostInterace> {
	static constexpr bool readable = true;
	static constexpr bool writable = true;

	dsp56k::TWord read(dsp56720::SerialHostInterace& shi) {
		try {
			return shi.readTX();
		} catch(dsp56720::QueueShutdown&) {
			throw vfs::Abort{};
		}
	}

	void write(dsp56720::SerialHostInterace& shi, dsp56k::TWord value) {
		try {
			shi.writeRX(value);
		} catch(dsp56720::QueueShutdown&) {
			throw vfs::Abort{};
		}
	}
};

std::string format(const char* format, ...) {
	char buffer[PATH_MAX];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	return std::string(buffer, strnlen(buffer, sizeof(buffer)));
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
	dsp56720::Debugger debugger{false};

	for (size_t i = 0; i < esai.outputs(); i++) {
		fs.tree().put(format("/peripherals/esai/output%d", i),
				vfs::SequentialFile<uint32_t,
					dsp56720::EnhancedSerialAudioInterface::Output>{
						esai.output(i)});
	}

	for (size_t i = 0; i < esai.inputs(); i++) {
		fs.tree().put("/peripherals/esai/input" + std::to_string(i),
				vfs::SequentialFile<uint32_t,
					dsp56720::EnhancedSerialAudioInterface::Input>{
						esai.input(i)});
	}

	fs.tree().put("/peripherals/shi0",
			vfs::SequentialFile<uint32_t, dsp56720::SerialHostInterace>{shi0});

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
				debugger.exec(dsp);
			}
		} catch(dsp56720::QueueShutdown&) {
			return;
		}
	});

	g_signalHandler = [&](auto signal) {
		std::cout << "INTERRUPTED!" << std::endl;

		running = false;
		dsp.terminate();

		// Wake debugger threads to allow everything to terminate
		debugger.continueExecution();

		fs.shutdown();
	};

	int ret = fs.run();
	mainloop.join();

	return ret;
}
