#pragma once

#include "Log.h"

#include <exception>
#include <string_view>

namespace cs::engine
{
	// Restores the previous value so nested engine calls unwind.
	template <class T>
	class ThreadLocalPointerScope
	{
	public:
		ThreadLocalPointerScope(T*& a_slot, T* a_value) noexcept :
			_slot(a_slot),
			_previous(a_slot)
		{
			_slot = a_value;
		}

		~ThreadLocalPointerScope() noexcept { _slot = _previous; }

		ThreadLocalPointerScope(const ThreadLocalPointerScope&) = delete;
		ThreadLocalPointerScope& operator=(const ThreadLocalPointerScope&) = delete;

	private:
		T*& _slot;
		T* _previous;
	};

	// Runs an engine patch, logging the outcome instead of throwing.
	template <class Install>
	bool TryPatch(spdlog::logger* a_log, std::string_view a_label, Install&& a_install)
	{
		try {
			a_install();
			a_log->info("Patched {}", a_label);
			return true;
		} catch (const std::exception& e) {
			a_log->error("Failed to patch {}: {}", a_label, e.what());
		} catch (...) {
			a_log->error("Failed to patch {}.", a_label);
		}
		return false;
	}
}
