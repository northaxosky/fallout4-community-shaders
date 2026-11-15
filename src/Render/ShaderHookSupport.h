#pragma once

#include "Log.h"

#include <exception>
#include <string_view>

namespace cs::engine
{
	// Restores the previous value so nested engine calls on one thread unwind correctly.
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
	bool TryPatch(std::string_view a_label, Install&& a_install)
	{
		auto* log = cs::log::Get("cs.render.shadersubclass");
		try {
			a_install();
			log->info("Patched {}", a_label);
			return true;
		} catch (const std::exception& e) {
			log->error("Failed to patch {}: {}", a_label, e.what());
		} catch (...) {
			log->error("Failed to patch {}.", a_label);
		}
		return false;
	}
}
