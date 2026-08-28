#pragma once

#include <concepts>
#include <span>
#include <string_view>

namespace cs::settings
{
	enum class RestartChange
	{
		Any,
		Enabling
	};

	template <class Settings>
	inline constexpr char RestartSettingsTypeToken = 0;

	struct RestartFieldInfo
	{
		using RequiresRestart = bool (*)(const void*, const void*);

		std::string_view label;
		const void* settingsType = nullptr;
		RequiresRestart requiresRestart = nullptr;
	};

	struct RestartSettingsView
	{
		std::span<const RestartFieldInfo> fields;
		const void* bootValues = nullptr;
		const void* currentValues = nullptr;
		const void* settingsType = nullptr;

		bool IsRestartRequired(const RestartFieldInfo& a_field) const noexcept
		{
			return bootValues && currentValues && settingsType &&
			       settingsType == a_field.settingsType && a_field.requiresRestart &&
			       a_field.requiresRestart(bootValues, currentValues);
		}
	};

	template <class Settings>
	constexpr RestartSettingsView MakeRestartSettingsView(
		std::span<const RestartFieldInfo> a_fields,
		const Settings& a_bootValues,
		const Settings& a_currentValues) noexcept
	{
		return {
			a_fields,
			&a_bootValues,
			&a_currentValues,
			&RestartSettingsTypeToken<Settings>
		};
	}

	template <auto Member>
	struct RestartMemberTraits;

	template <class Settings, class Field, Field Settings::*Member>
	struct RestartMemberTraits<Member>
	{
		using SettingsType = Settings;
		using FieldType = Field;
	};

	template <auto Member, RestartChange Change>
	consteval RestartFieldInfo MakeRestartField(std::string_view a_label)
	{
		using Traits = RestartMemberTraits<Member>;
		using Settings = typename Traits::SettingsType;
		using Field = typename Traits::FieldType;

		static_assert(std::equality_comparable<Field>);
		if constexpr (Change == RestartChange::Enabling)
			static_assert(std::convertible_to<Field, bool>);

		return {
			a_label,
			&RestartSettingsTypeToken<Settings>,
			[](const void* a_lhs, const void* a_rhs) {
				const auto& lhs = static_cast<const Settings*>(a_lhs)->*Member;
				const auto& rhs = static_cast<const Settings*>(a_rhs)->*Member;
				if constexpr (Change == RestartChange::Enabling)
					return !static_cast<bool>(lhs) && static_cast<bool>(rhs);
				else
					return lhs != rhs;
			}
		};
	}
}

#define CS_RESTART_FIELD(settingsType, member, label) \
	::cs::settings::MakeRestartField<&settingsType::member, ::cs::settings::RestartChange::Any>(label)

#define CS_RESTART_ENABLE_FIELD(settingsType, member, label) \
	::cs::settings::MakeRestartField<&settingsType::member, ::cs::settings::RestartChange::Enabling>(label)
