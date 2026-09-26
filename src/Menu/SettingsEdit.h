#pragma once

#include "Feature.h"

#include <DearModdingUI/UI.h>

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
