#pragma once

#include <unordered_set>

namespace dsp56720 {
class Debugger {
public:
	Debugger(bool stopped) : m_stopped(stopped) {}

	void setBreakpoint(dsp56k::TWord address) { m_breakpoints.emplace(address); }
	void removeBreakpoint(dsp56k::TWord address) { m_breakpoints.erase(address); }

	void continueExecution(size_t n = 0) {
		m_instruction_counter = n;
		m_stopped = false;
		m_state_changed.notify_one();
	}

	void exec(dsp56k::DSP& core) {
		if (m_stopped) {
			waitUntilContinue();
		}

		auto pc = core.getPC().toWord();
		if (m_breakpoints.find(pc) != m_breakpoints.end()) {
			stop();
			return;
		}

		core.exec();

		if (m_instruction_counter) {
			m_instruction_counter--;

			if (!m_instruction_counter) {
				stop();
			}
		}
	}

private:
	void stop()  {
		m_stopped = true;
		m_state_changed.notify_one();
	}

	void waitUntilContinue() {
		std::unique_lock<std::mutex> lock(m_mutex);

		while (m_stopped) {
			m_state_changed.wait(lock);
		}
	}

	std::mutex m_mutex;
	std::condition_variable m_not_full, m_state_changed;

	std::unordered_set<dsp56k::TWord> m_breakpoints;
	size_t m_instruction_counter;
	bool m_stopped;
};
}
