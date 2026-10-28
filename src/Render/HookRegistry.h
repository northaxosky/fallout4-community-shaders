#pragma once

#include "Log.h"
#include "Render/RenderHooks.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace cs::engine
{
	// Entries run in priority order; equal priorities keep registration order.
	template <class Entry>
	void InsertByPriority(std::vector<Entry>& a_entries, Entry a_entry)
	{
		a_entries.push_back(std::move(a_entry));
		std::ranges::stable_sort(a_entries, {}, [](const Entry& a_item) { return static_cast<int>(a_item.priority); });
	}

	// Detours a_id once; false, and logged, if the hook did not attach.
	template <class Hook>
	bool EnsureDetourInstalled(bool& a_installed, REL::VariantID a_id, const char* a_name)
	{
		if (a_installed) {
			return true;
		}
		auto* log = cs::log::Get("cs.hooks");
		stl::detour_thunk<Hook>(a_id);
		if (Hook::func.address() == 0) {
			log->error("Hook failed on {}", a_name);
			return false;
		}
		a_installed = true;
		log->info("Hook installed on {}", a_name);
		return true;
	}
}
