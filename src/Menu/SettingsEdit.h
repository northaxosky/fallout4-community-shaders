#pragma once

#include "Feature.h"
#include "Menu/SettingWidgets.h"

#include <DearModdingUI/Presentation.h>
#include <DearModdingUI/UI.h>

#include <span>
#include <string>
#include <vector>

namespace cs::settings
{
	// Records edits on the feature; HostClient persists them on completion or page exit.
	class SettingsEdit
	{
	public:
		explicit SettingsEdit(Feature& a_feature) noexcept : _feature(a_feature) {}

		bool Continuous(bool a_changed) noexcept
		{
			return Record(a_changed, dmui::ui::IsItemDeactivatedAfterEdit());
		}

		bool Discrete(bool a_changed) noexcept
		{
			return Record(a_changed, a_changed);
		}

		bool Apply(ui::EditResult a_result) noexcept
		{
			return Record(a_result.changed, a_result.completed);
		}

		// A schema bool as a checkbox labeled by its description, with a tooltip.
		template <class Field, class Settings>
		bool Toggle(const Field& a_field, Settings& a_settings, const char* a_tooltip)
		{
			const std::string label = ui::FieldLabel(a_field);
			const bool changed = Discrete(dmui::ui::Checkbox(label.c_str(), &(a_settings.*a_field.member)));
			ui::Tooltip(a_tooltip);
			return changed;
		}

		// A schema choice as a combo labeled by its description, with a tooltip.
		template <class Field, class Settings>
		bool Choice(const Field& a_field, Settings& a_settings, const char* a_tooltip)
		{
			using Type = typename Field::ValueType;
			std::vector<dmui::ChoiceOption<Type>> options;
			for (const auto& choice : a_field.choices)
				options.push_back({ choice.value, std::string(choice.text), std::string(choice.text), true });
			auto& value = a_settings.*a_field.member;
			const auto result = dmui::DrawChoice<Type>(a_field.key, value,
				std::span<const dmui::ChoiceOption<Type>>{ options }, "Unavailable", a_field.description);
			const bool changed = Discrete(result.changed);
			if (changed)
				value = *result.selected;
			ui::Tooltip(a_tooltip);
			return changed;
		}

	private:
		bool Record(bool a_changed, bool a_completed) noexcept
		{
			_feature._settingsSavePending |= a_changed;
			_feature._settingsEditCompleted |= a_completed;
			return a_changed;
		}

		Feature& _feature;
	};
}
