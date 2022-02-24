#include <condition_variable>
#include <mutex>
#include <atomic>
#include <array>

namespace dsp56720 {
template <typename T, size_t N>
class CircularBuffer {
public:
	CircularBuffer() : m_head(0), m_tail(0), m_size(0) {}

	void pushBack(const T& value) {
		m_data[m_head++] = value;
		m_head %= N;
		++m_size;
	}

	T popFront() {
		T value = m_data[m_tail++];
		m_tail %= N;
		--m_size;
		return value;
	}

	T& front() {
		return m_data[m_tail];
	}

	bool empty() const {
		return m_size == 0;
	}

	bool full() const {
		return m_size == N;
	}

private:
	size_t m_head;
	size_t m_tail;
	std::array<T, N> m_data;
	size_t m_size;
};

struct QueueShutdown : public std::exception {};

template <typename T, typename Container>
class Queue {
public:
	Queue() : m_shutdown(false) {}

	void push(const T& value) {
		std::unique_lock<std::mutex> lock(m_mutex);
		while (m_container.full()) {
			m_not_full.wait(lock);
			if (m_shutdown) {
				throw QueueShutdown();
			}
		}

		m_container.pushBack(value);
		m_not_empty.notify_one();
	}

	T& front() {
		return m_container.front();
	}

	T pop() {
		std::unique_lock<std::mutex> lock(m_mutex);
		while (m_container.empty()) {
			m_not_empty.wait(lock);
			if (m_shutdown) {
				throw QueueShutdown();
			}
		}

		T value = m_container.popFront();
		m_not_full.notify_one();
		return value;
	}

	void shutdown() {
		m_shutdown = true;
		m_not_full.notify_all();
		m_not_empty.notify_all();
	}

	bool empty() const { return m_container.empty(); }
	bool full() const { return m_container.full(); }

private:
	std::mutex m_mutex;
	std::condition_variable m_not_full, m_not_empty;

	std::atomic<bool> m_shutdown;
	Container m_container;
};
}
