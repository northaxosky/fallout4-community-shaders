#pragma once

#include "Feature.h"
#include "Menu/SettingWidgets.h"

#include <DearModdingUI/UI.h>

#include <string>

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

		// A schema bool as a checkbox labeled by its description, with help text.
		template <class Field, class Settings>
		bool Toggle(const Field& a_field, Settings& a_settings, const char* a_help)
		{
			return Apply(ui::EditSetting(a_field, a_settings, a_help));
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
