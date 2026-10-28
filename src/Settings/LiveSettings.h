#pragma once

#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <exception>
#include <mutex>

namespace cs::settings
{
	inline bool liveComparisonActive = false;

	struct PreparedLiveSettings
	{
		std::function<void()> swap;
		std::function<void()> finalize;
	};

	struct LiveSettingsAccess
	{
		std::function<toml::table()> snapshot;
		std::function<std::optional<PreparedLiveSettings>(const toml::table&, std::string&)> prepare;
		// Live-settable keys that an A/B comparison must not capture.
		std::vector<std::string_view> notComparable;
	};

	template <class... Fields>
	LiveSettingsAccess BindLiveSettings(
		const Schema<Fields...>& a_schema,
		typename Schema<Fields...>::SettingsType& a_settings,
		std::function<void()> a_finalize = {},
		std::vector<std::string_view> a_notComparable = {},
		std::function<bool(const typename Schema<Fields...>::SettingsType&, std::string&)> a_validate = {},
		std::mutex* a_swapGuard = nullptr)
	{
		const auto included = [](const auto& a_field) {
			return a_field.timing == ApplyTiming::kImmediate;
		};
		return {
			[&a_schema, &a_settings, included] {
				toml::table snapshot;
				std::apply([&](const auto&... fields) {
					([&] {
						if (included(fields))
							fields.Write(snapshot, a_settings);
					}(),
						...);
				},
					a_schema.fields);
				return snapshot;
			},
			[&a_schema, &a_settings, included, a_swapGuard, finalize = std::move(a_finalize), validate = std::move(a_validate)](
				const toml::table& a_snapshot, std::string& a_error) -> std::optional<PreparedLiveSettings> {
				auto candidate = a_settings;
				bool valid = true;
				std::apply([&](const auto&... fields) {
					([&] {
						if (included(fields) && valid)
							valid = fields.Read(a_snapshot, candidate, a_error);
					}(),
						...);
				},
					a_schema.fields);
				if (!valid)
					return std::nullopt;
				if (validate && !validate(candidate, a_error))
					return std::nullopt;
				return PreparedLiveSettings{
					[&a_schema, &a_settings, included, a_swapGuard, candidate = std::move(candidate)]() mutable {
						std::unique_lock<std::mutex> guard;
						if (a_swapGuard)
							guard = std::unique_lock<std::mutex>(*a_swapGuard);
						// FO4: swap only live fields so concurrent startup edits retain their restart requirement.
						std::apply([&](const auto&... fields) {
							([&] {
								if (included(fields)) {
									using std::swap;
									swap(a_settings.*fields.member, candidate.*fields.member);
								}
							}(),
								...);
						},
							a_schema.fields);
					},
					finalize
				};
			},
			std::move(a_notComparable)
		};
	}

	inline void ApplyPreparedLiveSettings(std::span<PreparedLiveSettings> a_prepared)
	{
		for (auto& value : a_prepared)
			value.swap();
		try {
			for (auto& value : a_prepared) {
				if (value.finalize)
					value.finalize();
			}
		} catch (...) {
			const auto failure = std::current_exception();
			for (auto& value : a_prepared)
				value.swap();
			for (auto& value : a_prepared) {
				try {
					if (value.finalize)
						value.finalize();
				} catch (...) {
				}
			}
			std::rethrow_exception(failure);
		}
	}
}
