#define FUSE_USE_VERSION 35

#include <dsp56kEmu/dsp.h>
#include <fuse.h>
#include <cstring>
#include <csignal>

#include "dsp56720/peripherals.h"
#include "dsp56720/bitfield.h"
#include "dsp56720/shi.h"
#include "dsp56720/esai.h"
#include "dsp56720/cgm.h"
#include "dsp56720/ccm.h"
#include "dsp56720/chipid.h"

#include <iostream>

class File {
public:
	virtual std::size_t read(char *buf, std::size_t count, std::size_t pos) = 0;
	virtual std::size_t write(const char *buf, std::size_t count, std::size_t pos) = 0;
	virtual std::size_t size() = 0;
};

#include <filesystem>
#include <unordered_set>
class FileSystem {
public:
	std::unordered_set<std::string> list(std::string prefix) {
		// Prefix must have a trailing slash
		if (prefix.back() != '/') {
			prefix += '/';
		}

		std::unordered_set<std::string> unique;
		for (auto const& pair : m_files) {
			auto const& path = pair.first;

			if (path.rfind(prefix, 0) != 0) {
				continue;
			}

			auto start = prefix.size();
			auto end = path.find('/', start);

			unique.emplace(path.substr(start, end-start));
		}

		return unique;
	}

	std::shared_ptr<File> get(std::string filename) {
		auto it = m_files.find(filename);
		if (it == m_files.end()) {
			return nullptr;
		}

		return it->second;
	}

	bool exists(std::string prefix) {
		for (auto const& pair : m_files) {
			auto const& path = pair.first;

			if (path.rfind(prefix, 0) == 0) {
				return true;
			}
		}

		return false;
	}

	template <typename T>
	void put(std::string filename, T file) {
		m_files[filename] = std::make_shared<T>(file);
	}

private:
	std::unordered_map<std::string, std::shared_ptr<File>> m_files;
};

FileSystem fs;

static int dsp56720_open(const char *path, struct fuse_file_info *fi) {
	std::cerr << "open: " << path << std::endl;
	auto file = fs.get(path);
	if (!file) {
		return -ENOENT;
	}

	return 0;
}

static int dsp56720_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	std::cerr << "read: " << path << std::endl;
	auto file = fs.get(path);
	if (!file) {
		return -ENOENT;
	}


	return file->read(buf, size, offset);
};

static int dsp56720_write(const char *path, const char *buf, size_t size,
		      off_t offset, struct fuse_file_info *fi)
{
	std::cerr << "write: " << path << std::endl;
	auto file = fs.get(path);
	if (!file) {
		return -ENOENT;
	}

	return file->write(buf, size, offset);
}

static int dsp56720_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi,
			enum fuse_readdir_flags flags)
{
	std::cerr << "readdir: " << path << std::endl;
	if (!fs.exists(path)) {
		return -ENOENT;
	}

	filler(buf, ".", NULL, 0, fuse_fill_dir_flags(0));
	filler(buf, "..", NULL, 0, fuse_fill_dir_flags(0));

	for (auto const& filename : fs.list(path)) {
		filler(buf, filename.c_str(), NULL, 0, fuse_fill_dir_flags(0));
	}

	return 0;
}

static int dsp56720_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
	std::cerr << "getattr: " << path << std::endl;
	if (fs.exists(path)) {
		auto file = fs.get(path);

		if (file) {
			stbuf->st_mode = 0755 | S_IFREG;
			stbuf->st_nlink = 1;
			stbuf->st_size = file->size();
		} else {
			stbuf->st_mode = 0755 | S_IFDIR;
			stbuf->st_nlink = 2;
		}

		return 0;
	}

	return -ENOENT;
}

static const struct fuse_operations operations = {
	.getattr = dsp56720_getattr,
	.open    = dsp56720_open,
	.read	 = dsp56720_read,
	.write	 = dsp56720_write,
	.readdir = dsp56720_readdir,
};

class PinInterface : public File {
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

class SerialInterface : public File {
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

class AudioInputInterface : public File {
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

class AudioOutputInterface : public File {
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
	std::atomic<bool> running{true};
	std::atomic<bool> reset, moda0;

	// TODO: Use a EnumInterface mapping '0' -> 0 and '1' -> 1
	fs.put("/pins/reset", PinInterface{reset});
	fs.put("/pins/moda0", PinInterface{moda0});

	dsp56720::ClockGenerationModule cgm;
	dsp56720::ChipConfigurationModule ccm;
	dsp56720::SerialHostInterace shi0;
	dsp56720::EnhancedSerialAudioInterface esai{cgm};
	dsp56720::ChipIdentification chidr{0};

	fs.put("/peripherals/shi0", SerialInterface{shi0});
	fs.put("/peripherals/esai/input0", AudioInputInterface{esai, 0});
	fs.put("/peripherals/esai/ouput0", AudioOutputInterface{esai, 0});
	fs.put("/peripherals/esai/ouput1", AudioOutputInterface{esai, 1});
	fs.put("/peripherals/esai/ouput2", AudioOutputInterface{esai, 2});

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
			std::cout << HEX(dsp.getPC().toWord()) << std::endl;
			dsp.exec();
		}
	});
	struct fuse* fuse = NULL;

	g_signalHandler = [&](auto signal) {
		std::cout << "INTERRUPTED!" << std::endl;

		running = false;
		dsp.terminate();

		if (fuse) {
			fuse_exit(fuse);
		}
	};

	// TODO: Do we have to pass fuse args?
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	fuse = fuse_new(&args, &operations, sizeof(operations), NULL);
	if (!fuse) {
		return 1;
	}

	if (fuse_mount(fuse, "./mount")) {
		fuse_destroy(fuse);
		return 1;
	}

	struct fuse_loop_config cfg = { .max_idle_threads = 10 };
	int ret = fuse_loop_mt(fuse, &cfg);

	fuse_unmount(fuse);
	fuse_destroy(fuse);

	mainloop.join();

	return ret;
}
