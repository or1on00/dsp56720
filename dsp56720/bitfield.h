#pragma once

namespace dsp56720 {
template <typename T>
struct BitField {
	template <int Offset>
	struct Bit {
		static const T Max = 1;
		static const T Mask = T(1) << Offset;

		Bit(T value) : m_value(value & Max) {}
		Bit(BitField<T> value) : m_value((value & Mask) >> Offset) {}

		operator bool() const { return m_value; }

	private:
		T m_value;
	};

	template <int Offset, int Width>
	struct Packed {
		static const T Max = (T(1) << Width) - 1;
		static const T Mask = Max << Offset;

		Packed(T value) : m_value(value & Max) {}
		Packed(BitField<T> value) : m_value((value & Mask) >> Offset) {}

		operator T() const { return m_value; }

	private:
		T m_value;
	};

	template <int Offset, int Width>
	struct Set {
		static const T Max = (T(1) << Width) - 1;
		static const T Mask = Max << Offset;

		Set(T value) : m_value(value & Max) {}
		Set(BitField<T> value) : m_value((value & Mask) >> Offset) {}

		bool test(size_t pos) const {
			return (1 << pos) & m_value;
		}

		operator T() const { return m_value; }

	private:
		T m_value;
	};

	operator T() const { return value; }

	T value;

	template <int Offset>
	BitField& operator|=(Bit<Offset> bit) {
		value = (value & ~Bit<Offset>::Mask) | bit << Offset;
		return *this;
	}

	// Prevent typos
	template <int Offset>
	BitField& operator=(Bit<Offset> bit) = delete;

	BitField& operator=(T v) {
		value = v;
		return *this;
	}
};
}
