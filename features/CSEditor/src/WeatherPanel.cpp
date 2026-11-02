#include "WeatherPanel.h"

#include "Feature.h"
#include "Host/HostClient.h"
#include "Menu/Section.h"
#include "Menu/SettingWidgets.h"
#include "World/EditorIDs.h"
#include "World/GameTime.h"
#include "World/Weather.h"
#include "World/WeatherControl.h"
#include "World/WeatherProfiles.h"

#include "RE/B/BGSShaderParticleGeometryData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/Sky.h"
#include "RE/T/TESWeather.h"

#include <DearModdingUI/UI.h>
#include <DearModdingUI/UI/Colors.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <numbers>
#include <string_view>

namespace cs::features::cs_editor
{
	namespace
	{
		using Data = RE::TESWeather::WeatherData;
		using Flag = RE::TESWeather::WeatherDataFlags;

		constexpr std::uint32_t kAllWeatherFlags = 0x7F;
		constexpr std::uint32_t kUnclassifiedFlag = 0x40;
		constexpr std::uint32_t kTrackedFlags = 0x3F;
		constexpr float kGameHourMax = 23.99f;
		constexpr float kTimeScaleMin = 0.1f;
		constexpr float kTimeScaleMax = 4000.0f;

		struct FlagInfo
		{
			Flag flag;
			const char* name;
			dmui::ui::Vec4 color;
		};

		// FO4: Snow bit marks dust/radstorms, aurora bits radstorms; highest first.
		constexpr std::array<FlagInfo, 6> kFlags{
			FlagInfo{ Flag::kRainy, "Rainy", { 0.4f, 0.7f, 1.0f, 1.0f } },
			FlagInfo{ Flag::kPermAurora, "Radstorm", { 0.5f, 1.0f, 0.3f, 1.0f } },
			FlagInfo{ Flag::kAuroraFollowsSun, "Radstorm Sun", { 0.7f, 1.0f, 0.5f, 1.0f } },
			FlagInfo{ Flag::kSnow, "Dusty", { 0.85f, 0.7f, 0.45f, 1.0f } },
			FlagInfo{ Flag::kCloudy, "Cloudy", { 0.7f, 0.7f, 0.7f, 1.0f } },
			FlagInfo{ Flag::kPleasant, "Pleasant", {} }
		};

		constexpr std::array<Flag, 6> kFilterOrder{
			Flag::kPleasant, Flag::kCloudy, Flag::kRainy, Flag::kSnow, Flag::kPermAurora, Flag::kAuroraFollowsSun
		};

		std::uint8_t Raw(const RE::TESWeather& a_weather, Data a_index)
		{
			return static_cast<std::uint8_t>(a_weather.weatherData[static_cast<std::size_t>(a_index)]);
		}

		std::uint32_t Flags(const RE::TESWeather& a_weather)
		{
			return Raw(a_weather, Data::kFlags);
		}

		const FlagInfo& Info(Flag a_flag)
		{
			return *std::ranges::find(kFlags, a_flag, &FlagInfo::flag);
		}

		dmui::ui::Vec4 FlagColor(Flag a_flag)
		{
			if (a_flag == Flag::kPleasant)
				return dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusSuccess);
			return Info(a_flag).color;
		}

		dmui::ui::Vec4 WeatherColor(const RE::TESWeather* a_weather)
		{
			const auto info = dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusInfo);
			if (!a_weather)
				return info;
			const auto flags = Flags(*a_weather);
			for (const auto& entry : kFlags) {
				if (flags & static_cast<std::uint32_t>(entry.flag))
					return FlagColor(entry.flag);
			}
			if (flags == 0)
				return dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusWarning);
			return info;
		}

		std::string FlagNames(const RE::TESWeather& a_weather)
		{
			const auto flags = Flags(a_weather);
			if ((flags & kTrackedFlags) == 0)
				return "None";
			std::string names;
			for (const auto flag : kFilterOrder) {
				if (flags & static_cast<std::uint32_t>(flag)) {
					if (!names.empty())
						names += ", ";
					names += Info(flag).name;
				}
			}
			return names;
		}

		std::string FormatWeather(const RE::TESWeather* a_weather)
		{
			if (!a_weather)
				return "None";
			return std::format("{} [{}]", engine::DescribeWeather(a_weather), FlagNames(*a_weather));
		}

		bool MatchesSearch(std::string_view a_text, std::string_view a_search)
		{
			const auto lower = [](char a_c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(a_c))); };
			return std::ranges::search(a_text, a_search, {}, lower, lower).begin() != a_text.end() || a_search.empty();
		}

		void Bullet(const char* a_format, auto... a_args)
		{
			dmui::ui::Text("- %s", std::vformat(a_format, std::make_format_args(a_args...)).c_str());
		}

		void DisplayBasicInfo(const RE::TESWeather* a_weather, float a_pct)
		{
			if (!a_weather) {
				dmui::ui::Text("- No Weather Found");
				return;
			}
			dmui::ui::TextColored(WeatherColor(a_weather), "- %s", FormatWeather(a_weather).c_str());
			ui::Tooltip([&] {
				const auto editorID = engine::GetEditorID(a_weather);
				return std::format("Editor ID: {}\nForm ID: 0x{:08X}\nFlags: {}",
					editorID.empty() ? "None" : std::string(editorID), a_weather->GetFormID(), FlagNames(*a_weather));
			});
			if (a_pct >= 0.0f)
				Bullet("Weather Percentage: {:.1f}%", a_pct * 100.0f);
		}

		void DisplayPrecipitationInfo(const RE::TESWeather* a_weather)
		{
			if (!a_weather || !a_weather->precipitationData) {
				dmui::ui::Text("- Particle Density: No precipitation data");
				return;
			}
			// FO4: SPGD keeps Skyrim's DATA order; particle density is entry 11.
			constexpr std::size_t kParticleDensity = 11;
			const auto& settings = a_weather->precipitationData->data;
			if (settings.size() > kParticleDensity)
				Bullet("Particle Density: {:.3f}", settings[kParticleDensity].f);
			const auto* texture = a_weather->precipitationData->particleTexture.textureName.c_str();
			Bullet("Particle Texture: {}", texture && *texture ? texture : "None");
			const auto begin = Raw(*a_weather, Data::kBeginPrecip);
			const auto end = Raw(*a_weather, Data::kEndPrecip);
			Bullet("Precip Begin Fade-In: {:.3f} (raw {})", begin / 255.0f, begin);
			Bullet("Precip End Fade-Out: {:.3f} (raw {})", end / 255.0f, end);
			ui::Tooltip(
				"Precipitation fade transition parameters:\n"
				"Begin Fade-In: Point where precipitation starts appearing\n"
				"End Fade-Out: Point where precipitation fully disappears\n"
				"Raw values: 0-255 (uint8), Normalized: 0.0-1.0");
		}

		void DisplayLightningInfo(RE::TESWeather* a_weather, bool a_interactive)
		{
			if (!a_weather || Raw(*a_weather, Data::kThunderFreq) == 0)
				return;
			dmui::ui::Text("Lightning Color:");
			// The forwarding ABI has no color editor, so channels edit as 0-255 sliders.
			constexpr std::array<std::pair<Data, const char*>, 3> channels{
				std::pair{ Data::kLightningR, "R" }, std::pair{ Data::kLightningG, "G" }, std::pair{ Data::kLightningB, "B" }
			};
			dmui::ui::BeginDisabled(!a_interactive);
			for (const auto& [index, name] : channels) {
				int value = Raw(*a_weather, index);
				constexpr int min = 0, max = 255;
				const auto label = std::format("{}##lightning-{:08X}", name, a_weather->GetFormID());
				if (dmui::ui::SliderScalar(label.c_str(), &value, &min, &max))
					a_weather->weatherData[static_cast<std::size_t>(index)] = static_cast<std::int8_t>(static_cast<std::uint8_t>(value));
			}
			dmui::ui::EndDisabled();
			Bullet("Thunder Frequency: {}", Raw(*a_weather, Data::kThunderFreq));
			ui::Tooltip(
				"Thunder frequency raw value (0-255):\n\n"
				"Known data points from Creation Kit slider:\n"
				"- Raw 15 = ~100% frequency (highest thunder)\n"
				"- Raw 76 = ~75% frequency\n"
				"- Raw 203 = ~20% frequency\n"
				"- Raw 246 = ~5% frequency\n"
				"- Raw 255 = ~0% frequency (lowest thunder)\n\n"
				"Range: 0-255 (unsigned 8-bit integer)\n"
				"Note: Creation Kit interprets this value non-linearly");
			const auto begin = Raw(*a_weather, Data::kBeginThunder);
			const auto end = Raw(*a_weather, Data::kEndThunder);
			Bullet("Lightning Begin Fade-In: {:.3f} (raw {})", begin / 255.0f, begin);
			Bullet("Lightning End Fade-Out: {:.3f} (raw {})", end / 255.0f, end);
		}

		void DisplayWindInfo(const RE::TESWeather* a_weather, const RE::Sky& a_sky)
		{
			const auto speed = a_weather ? Raw(*a_weather, Data::kWindSpeed) : 0;
			if (!a_weather || (speed == 0 && a_sky.windSpeed <= 0.0f))
				return;
			Bullet("Weather Wind Speed: {:.2f} (raw {})", speed / 255.0f, speed);
			ui::Tooltip("Wind speed from weather definition");
			Bullet("Sky Wind Speed: {:.2f}", a_sky.windSpeed);
			ui::Tooltip("Current active wind speed from the sky system\nThis affects particle behavior and wind-based effects");
			const auto direction = Raw(*a_weather, Data::kWindDir);
			const auto range = Raw(*a_weather, Data::kWindDirRange);
			Bullet("Wind Direction: {:.1f}\xc2\xb0 (raw {})", direction * (360.0f / 256.0f), direction);
			Bullet("Wind Direction Range: {:.1f}\xc2\xb0 (raw {})", range * (180.0f / 256.0f), range);
			// FO4: wind-vs-player needs Skyrim's 30.5 degree offset; heading only.
			if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
				auto heading = std::fmod(player->data.angle.z * (180.0f / std::numbers::pi_v<float>), 360.0f);
				if (heading < 0.0f)
					heading += 360.0f;
				Bullet("Player Direction: {:.1f}\xc2\xb0", heading);
			}
		}

		void DisplayWeatherInfo(RE::TESWeather* a_weather, float a_pct, const RE::Sky& a_sky)
		{
			DisplayBasicInfo(a_weather, a_pct);
			DisplayPrecipitationInfo(a_weather);
			DisplayLightningInfo(a_weather, true);
			DisplayWindInfo(a_weather, a_sky);
		}

		bool HasCustomSettings(const RE::TESWeather* a_weather)
		{
			const auto key = engine::WeatherKey(a_weather);
			return !key.empty() && std::ranges::any_of(FeatureManager::Get().GetAll(), [&](Feature* a_feature) {
				const auto* profiles = a_feature->GetWeatherProfiles();
				return profiles && profiles->IsActive(key);
			});
		}
	}

	void WeatherPanel::DrawTimeControls()
	{
		const auto hour = engine::GetGameHour();
		const auto scale = engine::GetTimeScale();
		if (!hour || !scale)
			return;
		const bool paused = engine::IsGameTimePaused();
		if (dmui::ui::Button(paused ? "Resume Time" : "Pause Time")) {
			if (paused)
				engine::ResumeGameTime();
			else
				engine::PauseGameTime();
		}
		ui::Tooltip("Pause or resume game time progression");
		dmui::ui::SameLine();
		float gameHour = *hour;
		constexpr float hourMin = 0.0f, hourMax = kGameHourMax;
		if (dmui::ui::SliderScalar("Game Time", &gameHour, &hourMin, &hourMax, "%.2f"))
			engine::SetGameHour(gameHour);
		ui::Tooltip("Adjust the current game time");

		// Sync slider with actual value.
		if (paused)
			_timeScaleSlider = std::max(*scale, kTimeScaleMin);
		else if (std::abs(*scale - _timeScaleSlider) > 0.01f)
			_timeScaleSlider = *scale;
		if (dmui::ui::Button("Reset Speed")) {
			engine::ResetTimeScale();
			_timeScaleSlider = engine::kVanillaTimeScale;
		}
		ui::Tooltip("Reset time speed to vanilla (%.1fx)", engine::kVanillaTimeScale);
		dmui::ui::SameLine();
		dmui::ui::BeginDisabled(paused);
		constexpr float scaleMin = kTimeScaleMin, scaleMax = kTimeScaleMax;
		if (dmui::ui::SliderScalar("##TimeScale", &_timeScaleSlider, &scaleMin, &scaleMax,
				_timeScaleSlider == engine::kVanillaTimeScale ? "Vanilla Speed" : "%.1f", dmui::ui::SliderFlags::kLogarithmic))
			engine::SetTimeScale(_timeScaleSlider);
		dmui::ui::EndDisabled();
		dmui::ui::SameLine();
		dmui::ui::Text("%.1fx", *scale);
		ui::Tooltip("Adjust how fast time passes (vanilla: %.1fx)", engine::kVanillaTimeScale);
	}

	void WeatherPanel::DrawStatus(RE::Sky& a_sky)
	{
		auto* current = a_sky.currentWeather;
		if (!current) {
			dmui::ui::TextColored(dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusWarning), "No Active Weather");
			return;
		}
		if (HasCustomSettings(current)) {
			dmui::ui::TextColored(dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusSuccess), "Has Custom Settings");
		} else {
			dmui::ui::TextColored(dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusDisable), "Using Default Settings");
		}
		dmui::ui::Text("Current Weather: %s", engine::DescribeWeather(current).c_str());

		const float pct = std::clamp(a_sky.currentWeatherPct, 0.0f, 1.0f);
		const bool transitioning = a_sky.lastWeather && pct < 1.0f;
		// Always reserve space for transition info to prevent UI shifting.
		if (transitioning)
			dmui::ui::Text("Transitioning From: %s", engine::DescribeWeather(a_sky.lastWeather).c_str());
		else
			dmui::ui::Text("Transitioning From: No Transition");

		// Show background color when transition is complete.
		if (!transitioning)
			dmui::ui::PushStyleColor(dmui::ui::Color::kPlotHistogram, dmui::ui::GetStyleColor(dmui::ui::Color::kFrameBg));
		const auto overlay = transitioning ? std::format("Transition: {:.1f}%", pct * 100.0f) : std::string{};
		dmui::ui::ProgressBar(transitioning ? pct : 1.0f, { -1.0f, 0.0f }, overlay.c_str());
		if (!transitioning)
			dmui::ui::PopStyleColor();
	}

	void WeatherPanel::UpdateFilteredWeathers()
	{
		_filtered.clear();
		for (auto* weather : engine::GetAllWeathers()) {
			const auto flags = Flags(*weather);
			// Unclassified includes weathers with no flags or only untracked flags.
			const bool include = _flagFilter == kAllWeatherFlags ||
			                     (flags & _flagFilter & kTrackedFlags) != 0 ||
			                     ((_flagFilter & kUnclassifiedFlag) && (flags & kTrackedFlags) == 0);
			if (include)
				_filtered.push_back(weather);
		}
	}

	void WeatherPanel::DrawControls(RE::Sky& a_sky)
	{
		dmui::ui::Text("Filter by Weather Type:");
		if (dmui::ui::Button("Select All"))
			_flagFilter = kAllWeatherFlags;
		dmui::ui::SameLine();
		if (dmui::ui::Button("Clear All"))
			_flagFilter = 0;

		// Dynamic checkbox layout - calculate how many fit per row.
		constexpr float checkboxWidth = 110.0f;
		const int perRow = std::max(1, static_cast<int>(dmui::ui::GetContentRegionAvail().x / checkboxWidth));
		for (std::size_t i = 0; i <= kFilterOrder.size(); ++i) {
			if (i > 0 && i % perRow != 0)
				dmui::ui::SameLine();
			const bool unclassified = i == kFilterOrder.size();
			const auto bit = unclassified ? kUnclassifiedFlag : static_cast<std::uint32_t>(kFilterOrder[i]);
			const auto color = unclassified ? dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusWarning) : FlagColor(kFilterOrder[i]);
			bool checked = (_flagFilter & bit) != 0;
			dmui::ui::PushStyleColor(dmui::ui::Color::kText, color);
			if (dmui::ui::Checkbox(unclassified ? "None" : Info(kFilterOrder[i]).name, &checked))
				_flagFilter = checked ? (_flagFilter | bit) : (_flagFilter & ~bit);
			dmui::ui::PopStyleColor();
			if (unclassified)
				ui::Tooltip(
					"Shows weathers that are not classified under any specific category.\n"
					"Includes weathers with no flags or only untracked flags.\n"
					"Categories tracked: Pleasant, Cloudy, Rainy, Dusty, Radstorm, Radstorm Sun");
		}

		// Update filtered weathers when the filter or the weather list changes.
		if (_lastFlagFilter != _flagFilter || _loadedCount != engine::GetAllWeathers().size()) {
			UpdateFilteredWeathers();
			_lastFlagFilter = _flagFilter;
			_loadedCount = engine::GetAllWeathers().size();
		}

		static_cast<void>(dmui::ui::Checkbox("Accelerate Weather Change", &_instantChange));
		ui::Tooltip("When enabled, weather changes instantly");
		if (dmui::ui::Button("Reset Weather")) {
			engine::ResetWeather();
			// Update the selection to reflect the reset weather without double-applying.
			_selected = a_sky.defaultWeather;
		}
		ui::Tooltip("Resets weather to default");

		dmui::ui::SameLine();
		const bool locked = engine::GetLockedWeather() != nullptr;
		if (locked)
			dmui::ui::PushStyleColor(dmui::ui::Color::kButton, dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusSuccess));
		if (dmui::ui::Button(locked ? "Unlock Weather" : "Lock Weather")) {
			if (locked)
				engine::UnlockWeather();
			else if (a_sky.currentWeather)
				engine::LockWeather(a_sky.currentWeather);
		}
		if (locked)
			dmui::ui::PopStyleColor();
		if (!engine::AreWeatherLockHooksInstalled())
			ui::Tooltip("Weather-lock hooks failed to install; the lock still works but weather may briefly flash before correcting");
		else
			ui::Tooltip(locked ? "Unlock weather to allow natural changes" : "Lock current weather to prevent changes");

		DrawWeatherCombo();
	}

	void WeatherPanel::DrawWeatherCombo()
	{
		const auto preview = _selected ? FormatWeather(_selected) : std::string("Select Weather");
		if (!dmui::ui::BeginCombo("Weather", preview.c_str())) {
			_search.clear();
			return;
		}
		static_cast<void>(host::HostClient::Get().Client().DrawSearchInput("##cs-editor-weather-search", "Search", _search));
		for (auto* weather : _filtered) {
			const auto label = FormatWeather(weather);
			// Filter by editor ID and form ID only, not classification tags.
			if (!MatchesSearch(engine::DescribeWeather(weather), _search) &&
				!MatchesSearch(std::format("{:08X}", weather->GetFormID()), _search))
				continue;
			const bool isSelected = weather == _selected;
			dmui::ui::PushStyleColor(dmui::ui::Color::kText, WeatherColor(weather));
			const bool picked = dmui::ui::Selectable(std::format("{}##{:08X}", label, weather->GetFormID()).c_str(), isSelected);
			dmui::ui::PopStyleColor();
			if (picked) {
				_selected = weather;
				engine::ApplyWeather(weather, _instantChange);
				_search.clear();
				break;
			}
			ui::Tooltip([&] {
				const auto editorID = engine::GetEditorID(weather);
				return std::format("Editor ID: {}\nForm ID: 0x{:08X}",
					editorID.empty() ? "None" : std::string(editorID), weather->GetFormID());
			});
			if (isSelected)
				dmui::ui::SetItemDefaultFocus();
		}
		dmui::ui::EndCombo();
	}

	void WeatherPanel::DrawInformation(RE::Sky& a_sky)
	{
		// Keep the last weather once the engine clears it.
		if (a_sky.lastWeather)
			_cachedLastWeather = a_sky.lastWeather;
		auto* last = a_sky.lastWeather ? a_sky.lastWeather : _cachedLastWeather;
		using dmui::ui::TableFlags;
		if (!dmui::ui::BeginTable("cs-editor-weather-comparison", 2, TableFlags::kResizable | TableFlags::kBorders))
			return;
		dmui::ui::TableSetupColumn("Current Weather", dmui::ui::TableColumnFlags::kWidthStretch, 0.5f);
		dmui::ui::TableSetupColumn("Last Weather", dmui::ui::TableColumnFlags::kWidthStretch, 0.5f);
		dmui::ui::TableHeadersRow();
		dmui::ui::TableNextRow();
		static_cast<void>(dmui::ui::TableNextColumn());
		dmui::ui::PushID("current");
		DisplayWeatherInfo(a_sky.currentWeather, a_sky.currentWeatherPct, a_sky);
		dmui::ui::PopID();
		static_cast<void>(dmui::ui::TableNextColumn());
		dmui::ui::PushID("last");
		DisplayWeatherInfo(last, std::abs(a_sky.currentWeatherPct - 1.0f), a_sky);
		dmui::ui::PopID();
		dmui::ui::EndTable();
	}

	void WeatherPanel::DrawFeatureAnalysis(const Feature& a_editor)
	{
		for (auto* feature : FeatureManager::Get().GetAll()) {
			const auto title = feature->GetWeatherAnalysisTitle();
			if (feature == &a_editor || title.empty())
				continue;
			const auto id = std::format("cs-editor-analysis-{}", feature->GetName());
			if (const ui::Section section{ id.c_str(), std::string(title).c_str() }; section)
				feature->DrawWeatherAnalysis();
		}
	}

	RE::TESWeather* WeatherPanel::GetTargetWeather(const RE::Sky& a_sky) const
	{
		return _selected ? _selected : a_sky.currentWeather;
	}
}
