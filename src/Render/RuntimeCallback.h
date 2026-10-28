#pragma once

#include "Feature.h"
#include "Log.h"

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cs::render
{
	inline void LogRenderCallbackFailure(std::string_view a_label, std::string_view a_reason) noexcept
	{
		try {
			using Clock = std::chrono::steady_clock;
			static thread_local std::unordered_map<std::string, Clock::time_point> lastLog;
			const auto now = Clock::now();
			const auto [entry, first] = lastLog.try_emplace(std::string(a_label), now);
			if (!first && now - entry->second < std::chrono::seconds(2))
				return;
			entry->second = now;
			cs::log::Get("cs.hooks")->error("{} callback failed: {}", a_label, a_reason);
		} catch (...) {
		}
	}

	template <class Fn>
	bool RunRenderCallback(Feature* a_owner, std::string_view a_label, Fn&& a_callback) noexcept
	{
		if (a_owner) {
			bool completed = false;
			FeatureManager::Get().RunRuntimeCallback(*a_owner, a_label, [&] {
				a_callback();
				completed = true;
			});
			return completed;
		}
		try {
			a_callback();
			return true;
		} catch (const std::exception& e) {
			LogRenderCallbackFailure(a_label, e.what());
		} catch (...) {
			LogRenderCallbackFailure(a_label, "non-standard exception");
		}
		return false;
	}
}
