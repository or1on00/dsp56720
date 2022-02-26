#pragma once

#include <type_traits>
#include "filesystem.h"

namespace vfs {
template <typename T>
struct SequentialAccess;

// This will discard data from anything that provides a too small buffer
template <typename Unit, typename T>
class SequentialFile : public File {
public:
	SequentialFile(T& device) : m_device(device) {}

	virtual std::size_t size() {
		return 4096;
	}

	virtual std::size_t read(char *buf, std::size_t count, std::size_t pos) override {
		if constexpr(SequentialAccess<T>::readable) {
			auto words = reinterpret_cast<Unit*>(buf);
			for (size_t i = 0; i < count / sizeof(Unit); i++) {
				*words++ = m_access.read(m_device);
			}

			return count;
		} else {
			return 0;
		}
	}

	virtual std::size_t write(const char *buf, std::size_t count, std::size_t pos) override {
		if constexpr(SequentialAccess<T>::writable) {
			auto words = reinterpret_cast<const Unit*>(buf);
			for (size_t i = 0; i < count / sizeof(Unit); i++) {
				m_access.write(m_device, *words++);
			}

			return count;
		} else {
			return 0;
		}
	}

private:
	T& m_device;
	SequentialAccess<T> m_access;
};
}
