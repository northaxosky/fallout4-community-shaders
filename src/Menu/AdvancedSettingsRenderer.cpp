#include "Menu/AdvancedSettingsRenderer.h"

#include "Feature.h"
#include "Log.h"
#include "Menu/Fonts.h"
#include "Menu/Menu.h"
#include "Plugin.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/Hotkey.h"
#include "Utils/UI.h"

#include <array>
#include <filesystem>
#include <format>
#include <string>

#include <shellapi.h>

#include <imgui.h>
#include <imgui_internal.h>

namespace
{
	using namespace cs;

	constexpr std::array<const char*, 7> kLevelNames = { "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off" };
	constexpr std::array<spdlog::level::level_enum, 7> kLevels = {
		spdlog::level::trace,
		spdlog::level::debug,
		spdlog::level::info,
		spdlog::level::warn,
		spdlog::level::err,
		spdlog::level::critical,
		spdlog::level::off
	};

	int LevelIndex(spdlog::level::level_enum a_level)
	{
		for (std::size_t i = 0; i < kLevels.size(); ++i) {
			if (kLevels[i] == a_level)
				return static_cast<int>(i);
		}
		return 2;
	}
}

namespace cs
{
	void AdvancedSettingsRenderer::RenderAdvancedSettings(const std::function<void()>& a_drawDisableAtBootSettings)
	{
		MenuFonts::TabBarPaddingGuard tabGuard(Menu::FontRole::Subheading);

		if (!ImGui::BeginTabBar("##AdvancedSettingsTabs"))
			return;

		if (MenuFonts::BeginTabItemWithFont("Developer", Menu::FontRole::Subheading)) {
			RenderDeveloperSection();
			ImGui::EndTabItem();
		}

		if (MenuFonts::BeginTabItemWithFont("Disable at Boot", Menu::FontRole::Subheading)) {
			RenderDisableAtBootSection(a_drawDisableAtBootSettings);
			ImGui::EndTabItem();
		}

		if (MenuFonts::BeginTabItemWithFont("Logging", Menu::FontRole::Subheading)) {
			RenderLoggingSection();
			ImGui::EndTabItem();
		}

		if (MenuFonts::BeginTabItemWithFont("Diagnostics", Menu::FontRole::Subheading)) {
			RenderDiagnosticsSection();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	void AdvancedSettingsRenderer::RenderDeveloperSection()
	{
		ImGui::Spacing();
		ui::DrawSectionHeader("Configuration", true, false);

		ImGui::TextWrapped("%s",
			"Feature settings save as you change them. The shipped Default TOML is never rewritten; "
			"in-game changes land in the sibling User TOML.");

		ImGui::Spacing();
		if (ImGui::Button("Open configuration folder")) {
			const auto dir = ui::paths::GetPluginPath();
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
	}

	void AdvancedSettingsRenderer::RenderDisableAtBootSection(const std::function<void()>& a_drawDisableAtBootSettings)
	{
		ImGui::Spacing();

		ImGui::TextWrapped("%s",
			"Select features to disable at boot. This is the same as removing the feature's config entry. "
			"A restart is required to re-enable one.");
		ImGui::Spacing();

		if (a_drawDisableAtBootSettings)
			a_drawDisableAtBootSettings();
	}

	void AdvancedSettingsRenderer::RenderLoggingSection()
	{
		ImGui::Spacing();

		int levelIdx = LevelIndex(log::GlobalLevel());
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
		if (ImGui::Combo("Global level", &levelIdx, kLevelNames.data(), static_cast<int>(kLevelNames.size()))) {
			log::SetGlobalLevel(kLevels[static_cast<std::size_t>(levelIdx)]);
			log::SaveConfigToToml();
		}

		ImGui::Spacing();
		ui::DrawSectionHeader("Telemetry", true, false);

		bool telemetryEnabled = telemetry::pump::Enabled();
		if (ImGui::Checkbox("Emit telemetry", &telemetryEnabled)) {
			telemetry::pump::SetEnabled(telemetryEnabled);
			log::SaveConfigToToml();
		}
		ImGui::SameLine();
		if (ImGui::Button("Dump now"))
			telemetry::pump::RequestDump();

		ImGui::Spacing();
		ui::DrawSectionHeader("Channels", true, false);

		static std::string loggerSearch;
		ui::DrawFeatureSearchBar(loggerSearch);

		const auto loggers = log::ListLoggers();
		if (ImGui::BeginChild("##LoggerList", ImVec2(0, ImGui::GetFontSize() * 12.0f), true)) {
			for (const auto& name : loggers) {
				if (!ui::StringMatchesSearch(name, loggerSearch))
					continue;

				ImGui::PushID(name.c_str());
				auto* logger = log::Get(name.c_str());
				int channelIdx = LevelIndex(logger ? logger->level() : log::GlobalLevel());
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
				if (ImGui::Combo("##level", &channelIdx, kLevelNames.data(), static_cast<int>(kLevelNames.size()))) {
					log::SetLevel(name.c_str(), kLevels[static_cast<std::size_t>(channelIdx)]);
					log::SaveConfigToToml();
				}
				ImGui::SameLine();
				ImGui::TextUnformatted(name.c_str());
				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		ImGui::Spacing();
		const auto dumpHotkey = log::GetDumpHotkey();
		ImGui::TextDisabled("Log dump hotkey: %s", dumpHotkey.IsBound() ? dumpHotkey.ToString().c_str() : "unbound");
	}

	void AdvancedSettingsRenderer::RenderDiagnosticsSection()
	{
		ImGui::Spacing();

		ImGui::Text("Plugin version: %s", ui::GetFormattedVersion().c_str());
		ImGui::Text("Build: %s", CS_BUILD_DESCRIBE);
		ImGui::Text("Commit: %s", CS_BUILD_GIT_SHA);
		ImGui::Text("GPU: %s", Menu::Get().GetAdapterDescription().c_str());

		ImGui::Spacing();
		ui::DrawSectionHeader("Feature states", true, false);

		if (ImGui::BeginTable("##FeatureStates", 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Installed", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			const auto& statusPalette = Menu::Get().GetTheme().StatusPalette;
			for (const Feature* feature : FeatureManager::Get().GetRegisteredFeatures()) {
				if (!feature)
					continue;

				const auto& state = feature->GetState();

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(std::string(feature->GetDisplayName()).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(feature->IsInstalled() ? "yes" : "no");
				ImGui::TableNextColumn();

				ImVec4 color = statusPalette.Disable;
				switch (state.runtimeState) {
				case FeatureRuntimeState::kActive:
					color = statusPalette.SuccessColor;
					break;
				case FeatureRuntimeState::kDegraded:
					color = statusPalette.Warning;
					break;
				case FeatureRuntimeState::kFailed:
					color = statusPalette.Error;
					break;
				default:
					break;
				}
				const auto stateName = FeatureRuntimeStateName(state.runtimeState);
				ImGui::TextColored(color, "%.*s", static_cast<int>(stateName.size()), stateName.data());

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(state.detail.empty() ? "-" : state.detail.c_str());
			}

			ImGui::EndTable();
		}
	}
}
