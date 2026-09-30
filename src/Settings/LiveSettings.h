#pragma once

#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <exception>

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
	};

	template <class... Fields>
	LiveSettingsAccess BindLiveSettings(
		const Schema<Fields...>& a_schema,
		typename Schema<Fields...>::SettingsType& a_settings,
		std::function<void()> a_finalize = {},
		std::vector<std::string_view> a_excluded = {},
		std::function<bool(const typename Schema<Fields...>::SettingsType&, std::string&)> a_validate = {})
	{
		const auto included = [excluded = std::move(a_excluded)](const auto& a_field) {
			return a_field.timing == ApplyTiming::kImmediate &&
			       std::ranges::find(excluded, a_field.key) == excluded.end();
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
			[&a_schema, &a_settings, included, finalize = std::move(a_finalize), validate = std::move(a_validate)](
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
					[&a_schema, &a_settings, included, candidate = std::move(candidate)]() mutable {
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
			}
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
