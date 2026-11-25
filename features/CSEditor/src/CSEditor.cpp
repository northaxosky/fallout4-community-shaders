#include "CSEditor.h"

#include "Menu/Section.h"
#include "World/EditorIDs.h"
#include "World/GameTime.h"
#include "World/Weather.h"
#include "World/WeatherControl.h"

#include "RE/S/Sky.h"

#include <DearModdingUI/UI.h>
#include <DearModdingUI/UI/Colors.h>

namespace cs::features
{
	CSEditor* CSEditor::GetSingleton()
	{
		static CSEditor instance;
		return &instance;
	}

	void CSEditor::Load()
	{
		// Editor IDs are recorded as forms load, so the cache must precede game data.
		engine::InstallEditorIDCache();
		engine::InstallWeatherLockHooks();
	}

	void CSEditor::OnDataLoaded()
	{
		engine::InstallGameTimeMenuGuard();
	}

	void CSEditor::Prepass()
	{
		engine::MaintainWeatherLock();
	}

	void CSEditor::DrawSettings()
	{
		const auto showError = [](const char* a_message) {
			dmui::ui::TextColored(dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusError), "%s", a_message);
		};

		if (const ui::Section section{ "cs-editor-time", "Time" }; section)
			_weather.DrawTimeControls();

		auto* sky = RE::Sky::GetSingleton();
		if (!sky) {
			showError("Sky not available");
			return;
		}
		if (sky->mode.get() != RE::Sky::Mode::kFull) {
			showError("Sky not in full mode");
			return;
		}
		if (engine::GetAllWeathers().empty())
			engine::LoadWeatherList();

		if (const ui::Section section{ "cs-editor-status", "Weather Status" }; section)
			_weather.DrawStatus(*sky);
		if (const ui::Section section{ "cs-editor-controls", "Weather Controls" }; section)
			_weather.DrawControls(*sky);
		if (const ui::Section section{ "cs-editor-information", "Weather Information" }; section)
			_weather.DrawInformation(*sky);
		_overrides.Draw(_weather.GetTargetWeather(*sky));
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(CSEditor::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
