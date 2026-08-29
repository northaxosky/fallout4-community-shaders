#include "Menu/SettingsTabRenderer.h"

#include "Log.h"
#include "Menu/BackgroundBlur.h"
#include "Menu/CursorLoader.h"
#include "Menu/FontSelector.h"
#include "Menu/Fonts.h"
#include "Menu/Menu.h"
#include "Menu/ThemeManager.h"
#include "Settings/FeatureConfig.h"
#include "Utils/ShaderCache/CacheStorage.h"
#include "Utils/UI.h"

#include <algorithm>
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

	// Names mirror ImGuiCol_ so the list stays readable when ImGui adds slots.
	const char* ImGuiColorName(int a_index)
	{
		const char* name = ImGui::GetStyleColorName(a_index);
		return name ? name : "Unknown";
	}

	void MarkThemeDirty()
	{
		auto& menu = Menu::Get();
		menu.pendingFontReload = true;
	}

	void TrackContinuousEdit(bool a_changed, bool& a_dirty, bool& a_commit)
	{
		a_dirty |= a_changed;
		a_commit |= ImGui::IsItemDeactivatedAfterEdit();
	}
}

namespace cs
{
	void SettingsTabRenderer::RenderGeneralSettings(SettingsState& a_state)
	{
		MenuFonts::TabBarPaddingGuard tabGuard(Menu::FontRole::Subheading);

		if (!ImGui::BeginTabBar("##GeneralSettingsTabs"))
			return;

		if (MenuFonts::BeginTabItemWithFont("Shaders", Menu::FontRole::Subheading)) {
			RenderShadersTab();
			ImGui::EndTabItem();
		}

		if (MenuFonts::BeginTabItemWithFont("Keybindings", Menu::FontRole::Subheading)) {
			RenderKeybindingsTab(a_state);
			ImGui::EndTabItem();
		}

		if (MenuFonts::BeginTabItemWithFont("Interface", Menu::FontRole::Subheading)) {
			RenderInterfaceTab();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	void SettingsTabRenderer::RenderHostedGeneralSettings()
	{
		MenuFonts::TabBarPaddingGuard tabGuard(Menu::FontRole::Subheading);

		if (!ImGui::BeginTabBar("##HostedGeneralSettingsTabs"))
			return;

		if (MenuFonts::BeginTabItemWithFont("Shaders", Menu::FontRole::Subheading)) {
			RenderShadersTab();
			ImGui::EndTabItem();
		}

		if (MenuFonts::BeginTabItemWithFont("Keybindings", Menu::FontRole::Subheading)) {
			ImGui::Spacing();
			ImGui::TextWrapped("%s",
				"The shared mod menu owns its open and close key. Feature hotkeys stay editable on "
				"each feature's own page and in the unified TOML.");
			ImGui::EndTabItem();
		}

		if (MenuFonts::BeginTabItemWithFont("Interface", Menu::FontRole::Subheading)) {
			ImGui::Spacing();
			ImGui::TextWrapped("%s",
				"The shared mod menu owns the theme, fonts, cursor, and background blur for every "
				"hosted mod. Community Shaders leaves them alone here; its own interface settings "
				"apply when it runs its standalone menu.");
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	void SettingsTabRenderer::RenderShadersTab()
	{
		ImGui::Spacing();

		const auto root = feature_config::GetMergedRoot();
		const auto ownership = feature_config::ParseShaderOwnership(root);

		ui::DrawSectionHeader("Shader Ownership", true, false);
		ImGui::TextWrapped("%s",
			"Community Shaders can substitute its own reconstructions for stock shaders. "
			"Replacement stays off unless it is enabled here and the stock shader hash matches.");
		ImGui::Spacing();

		const auto& statusPalette = Menu::Get().GetTheme().StatusPalette;
		ImGui::Text("Status:");
		ImGui::SameLine();
		if (ownership.config.enabled)
			ImGui::TextColored(statusPalette.SuccessColor, "enabled");
		else
			ImGui::TextColored(statusPalette.Disable, "disabled");

		if (!ownership.valid) {
			ui::Text::WrappedError("Configuration error: %s", ownership.error.c_str());
		}

		ImGui::Spacing();
		ImGui::BeginDisabled();
		bool deferredPrepass = ownership.config.targets.deferredPrepass;
		bool bsSky = ownership.config.targets.bsSky;
		bool bsWater = ownership.config.targets.bsWater;
		bool bsLighting = ownership.config.targets.bsLighting;
		bool bsdfLight = ownership.config.targets.bsdfLight;
		bool bsdfComposite = ownership.config.targets.bsdfComposite;
		ImGui::Checkbox("Deferred prepass", &deferredPrepass);
		ImGui::Checkbox("BSSky", &bsSky);
		ImGui::Checkbox("BSWater", &bsWater);
		ImGui::Checkbox("BSLighting", &bsLighting);
		ImGui::Checkbox("BSDF light", &bsdfLight);
		ImGui::Checkbox("BSDF composite", &bsdfComposite);
		ImGui::EndDisabled();

		if (auto tooltip = ui::HoverTooltipWrapper())
			ImGui::Text("%s", "Shader ownership is edited in FO4CommunityShaders.toml and applied at boot.");

		ImGui::Spacing();
		ui::DrawSectionHeader("Shader Cache", true, false);

		const auto cacheRoot = shader_cache::DefaultCacheRoot();
		ImGui::TextWrapped("Cache directory: %s", cacheRoot.string().c_str());

		if (ImGui::Button("Open cache folder"))
			ShellExecuteW(nullptr, L"open", cacheRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	void SettingsTabRenderer::RenderKeybindingsTab(SettingsState& a_state)
	{
		ImGui::Spacing();
		a_state.keybindingWidgetsActive.store(true, std::memory_order_release);

		auto& settings = Menu::Get().GetSettings();
		bool changed = false;

		const bool toggleWasRecording = a_state.settingToggleKey.load(std::memory_order_acquire);
		changed |= ui::InputComboWidget("Toggle Menu", settings.ToggleKey, a_state.settingToggleKey, "ToggleKey", false);
		if (!toggleWasRecording && a_state.settingToggleKey.load(std::memory_order_acquire))
			a_state.settingOverlayToggleKey.store(false, std::memory_order_release);

		const bool overlayWasRecording = a_state.settingOverlayToggleKey.load(std::memory_order_acquire);
		changed |= ui::InputComboWidget("Toggle Overlay", settings.OverlayToggleKey, a_state.settingOverlayToggleKey, "OverlayToggleKey");
		if (!overlayWasRecording && a_state.settingOverlayToggleKey.load(std::memory_order_acquire))
			a_state.settingToggleKey.store(false, std::memory_order_release);

		ImGui::Spacing();
		ImGui::TextDisabled("%s", "The menu binding cannot be cleared. Right-click the overlay binding to clear it.");

		if (changed)
			Menu::Get().Save();
	}

	void SettingsTabRenderer::RenderInterfaceTab()
	{
		MenuFonts::TabBarPaddingGuard tabGuard(Menu::FontRole::Subheading);

		if (!ImGui::BeginTabBar("##InterfaceTabs"))
			return;

		if (MenuFonts::BeginTabItemWithFont("Behavior", Menu::FontRole::Subheading)) {
			RenderBehaviorTab();
			ImGui::EndTabItem();
		}
		if (MenuFonts::BeginTabItemWithFont("Themes", Menu::FontRole::Subheading)) {
			RenderThemesTab();
			ImGui::EndTabItem();
		}
		if (MenuFonts::BeginTabItemWithFont("Fonts", Menu::FontRole::Subheading)) {
			Menu::Get().wantsFontPreviewAtlas = true;
			RenderFontsTab();
			ImGui::EndTabItem();
		}
		if (MenuFonts::BeginTabItemWithFont("Styling", Menu::FontRole::Subheading)) {
			RenderStylingTab();
			ImGui::EndTabItem();
		}
		if (MenuFonts::BeginTabItemWithFont("Colors", Menu::FontRole::Subheading)) {
			RenderColorsTab();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	void SettingsTabRenderer::RenderBehaviorTab()
	{
		ImGui::Spacing();

		auto& menu = Menu::Get();
		auto& settings = menu.GetSettings();
		auto& theme = settings.Theme;
		bool saveNow = false;
		static bool tooltipDirty = false;

		saveNow |= ImGui::Checkbox("Auto-hide the feature list", &settings.AutoHideFeatureList);
		if (auto tooltip = ui::HoverTooltipWrapper())
			ImGui::Text("%s", "Hides the left navigation column and gives the settings pane the full window width.");

		if (ImGui::Checkbox("Require Shift to dock windows", &settings.RequireShiftToDock)) {
			ImGui::GetIO().ConfigDockingWithShift = settings.RequireShiftToDock;
			saveNow = true;
		}

		saveNow |= ImGui::Checkbox("Show footer", &theme.ShowFooter);
		saveNow |= ImGui::Checkbox("Center header", &theme.CenterHeader);
		saveNow |= ImGui::Checkbox("Show action icons", &theme.ShowActionIcons);

		if (ImGui::Checkbox("Background blur", &theme.BackgroundBlurEnabled)) {
			BackgroundBlur::SetEnabled(theme.BackgroundBlurEnabled);
			saveNow = true;
		}

		if (ImGui::Checkbox("Use custom cursor", &theme.UseCustomCursor)) {
			menu.pendingCursorReload = true;
			saveNow = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d loaded)", CursorLoader::GetLoadedCount());

		bool tooltipCommit = false;
		TrackContinuousEdit(
			ImGui::SliderFloat("Tooltip delay", &theme.TooltipHoverDelay, 0.0f, 2.0f, "%.2f s"),
			tooltipDirty,
			tooltipCommit);
		if (tooltipCommit && tooltipDirty) {
			tooltipDirty = false;
			saveNow = true;
		}

		ImGui::Spacing();
		if (ImGui::Button("Reset window layout"))
			menu.resetLayout = true;

		if (saveNow)
			menu.Save();
	}

	void SettingsTabRenderer::RenderThemesTab()
	{
		ImGui::Spacing();

		auto& menu = Menu::Get();
		auto& themeManager = ThemeManager::Get();
		if (!themeManager.IsDiscovered())
			themeManager.DiscoverThemes();

		const auto names = themeManager.GetThemeNames();
		const auto& selected = menu.GetSettings().SelectedThemePreset;

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
		if (ImGui::BeginCombo("Import theme preset", selected.empty() ? "None" : selected.c_str())) {
			for (const auto& name : names) {
				const bool isSelected = name == selected;
				if (ImGui::Selectable(name.c_str(), isSelected)) {
					if (menu.LoadThemePreset(name)) {
						menu.GetSettings().SelectedThemePreset = name;
						menu.pendingFontReload = true;
						menu.pendingIconReload = true;
						menu.Save();
					}
				}
				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::Spacing();
		if (ImGui::Button("Refresh themes"))
			themeManager.RefreshThemes();
		ImGui::SameLine();
		if (ImGui::Button("Open themes folder")) {
			const auto dir = themeManager.GetThemesDirectory();
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		static std::string saveName;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
		char buffer[64];
		strncpy_s(buffer, saveName.c_str(), sizeof(buffer) - 1);
		buffer[sizeof(buffer) - 1] = '\0';
		if (ImGui::InputTextWithHint("##SaveThemeName", "New theme name", buffer, sizeof(buffer)))
			saveName = buffer;

		ImGui::SameLine();
		{
			ui::DisableGuard guard(saveName.empty());
			if (ui::ButtonWithFlash("Save as theme")) {
				toml::table themeTable;
				menu.SaveTheme(themeTable);
				if (themeManager.SaveTheme(saveName, themeTable, saveName, "User theme")) {
					menu.GetSettings().SelectedThemePreset = saveName;
					menu.Save();
					saveName.clear();
				}
			}
		}
	}

	void SettingsTabRenderer::RenderFontsTab()
	{
		ImGui::Spacing();

		auto& menu = Menu::Get();
		auto& settings = menu.GetSettings();
		static bool fontDirty = false;
		bool commit = false;

		if (ImGui::Checkbox("Scale font with screen resolution", &settings.UseResolutionFont)) {
			fontDirty = true;
			commit = true;
		}
		if (auto tooltip = ui::HoverTooltipWrapper())
			ImGui::Text("%s", "When off, the fixed base size below is used instead.");

		{
			ui::DisableGuard guard(settings.UseResolutionFont);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			const bool changed = ImGui::SliderFloat("Base font size", &settings.Theme.FontSize,
				ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE, "%.0f px");
			fontDirty |= changed;
			commit |= ImGui::IsItemDeactivatedAfterEdit();
			menu.fontEditActive |= ImGui::IsItemActive();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		for (std::size_t i = 0; i < static_cast<std::size_t>(Menu::FontRole::Count); ++i) {
			const auto role = static_cast<Menu::FontRole>(i);
			ui::DrawSectionHeader(std::string(Menu::GetFontRoleDisplayName(role)).c_str(), true, false);
			const auto result = FontSelector::DrawFontRoleSelector(role);
			fontDirty |= result.changed;
			commit |= result.commit;
			menu.fontEditActive |= result.active;
			ImGui::Spacing();
		}

		if (commit && fontDirty) {
			fontDirty = false;
			MarkThemeDirty();
			menu.Save();
		}

		ImGui::Spacing();
		if (ImGui::Button("Reload fonts"))
			menu.pendingFontReload = true;
		ImGui::SameLine();
		if (ImGui::Button("Open fonts folder")) {
			const auto dir = ui::paths::GetFontsPath();
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
	}

	void SettingsTabRenderer::RenderStylingTab()
	{
		ImGui::Spacing();

		auto& menu = Menu::Get();
		auto& theme = menu.GetTheme();
		auto& style = theme.Style;
		static bool dirty = false;
		bool commit = false;

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
		TrackContinuousEdit(ImGui::SliderFloat("Global scale", &theme.GlobalScale, -1.0f, 1.0f, "%.2f"), dirty, commit);
		if (auto tooltip = ui::HoverTooltipWrapper())
			ImGui::Text("%s", "Exponential; 0 is 100%.");

		ImGui::Spacing();
		ui::DrawSectionHeader("Spacing", true, false);
		TrackContinuousEdit(ImGui::SliderFloat2("Window padding", &style.WindowPadding.x, 0.0f, 24.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat2("Frame padding", &style.FramePadding.x, 0.0f, 24.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat2("Item spacing", &style.ItemSpacing.x, 0.0f, 24.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat2("Cell padding", &style.CellPadding.x, 0.0f, 24.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Indent spacing", &style.IndentSpacing, 0.0f, 32.0f, "%.0f"), dirty, commit);

		ImGui::Spacing();
		ui::DrawSectionHeader("Rounding", true, false);
		TrackContinuousEdit(ImGui::SliderFloat("Window rounding", &style.WindowRounding, 0.0f, 20.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Frame rounding", &style.FrameRounding, 0.0f, 20.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Tab rounding", &style.TabRounding, 0.0f, 20.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Grab rounding", &style.GrabRounding, 0.0f, 20.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Scrollbar rounding", &style.ScrollbarRounding, 0.0f, 20.0f, "%.0f"), dirty, commit);

		ImGui::Spacing();
		ui::DrawSectionHeader("Borders", true, false);
		TrackContinuousEdit(ImGui::SliderFloat("Window border", &style.WindowBorderSize, 0.0f, 4.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Child border", &style.ChildBorderSize, 0.0f, 4.0f, "%.0f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Frame border", &style.FrameBorderSize, 0.0f, 4.0f, "%.0f"), dirty, commit);

		ImGui::Spacing();
		ui::DrawSectionHeader("Scrollbar opacity", true, false);
		TrackContinuousEdit(ImGui::SliderFloat("Background##scroll", &theme.ScrollbarOpacity.Background, 0.0f, 1.0f, "%.2f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Thumb##scroll", &theme.ScrollbarOpacity.Thumb, 0.0f, 1.0f, "%.2f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Thumb hovered##scroll", &theme.ScrollbarOpacity.ThumbHovered, 0.0f, 1.0f, "%.2f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Thumb active##scroll", &theme.ScrollbarOpacity.ThumbActive, 0.0f, 1.0f, "%.2f"), dirty, commit);

		ImGui::Spacing();
		ui::DrawSectionHeader("Feature headings", true, false);
		TrackContinuousEdit(ImGui::SliderFloat("Title scale", &theme.FeatureHeading.FeatureTitleScale, 1.0f, 3.0f, "%.2f"), dirty, commit);
		TrackContinuousEdit(ImGui::SliderFloat("Minimized alpha", &theme.FeatureHeading.MinimizedFactor, 0.1f, 1.0f, "%.2f"), dirty, commit);

		if (commit && dirty) {
			dirty = false;
			menu.Save();
		}
	}

	void SettingsTabRenderer::RenderColorsTab()
	{
		ImGui::Spacing();

		auto& menu = Menu::Get();
		auto& theme = menu.GetTheme();
		static bool dirty = false;
		bool commit = false;

		ui::DrawSectionHeader("Palette", true, false);
		TrackContinuousEdit(ImGui::ColorEdit4("Background", &theme.FullPalette[ImGuiCol_WindowBg].x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Text", &theme.FullPalette[ImGuiCol_Text].x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Window border", &theme.FullPalette[ImGuiCol_Border].x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Frame border", &theme.FullPalette[ImGuiCol_FrameBg].x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Separator", &theme.FullPalette[ImGuiCol_Separator].x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Resize grip", &theme.FullPalette[ImGuiCol_ResizeGrip].x), dirty, commit);

		ImGui::Spacing();
		ui::DrawSectionHeader("Status colors", true, false);
		TrackContinuousEdit(ImGui::ColorEdit4("Disabled", &theme.StatusPalette.Disable.x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Error", &theme.StatusPalette.Error.x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Warning", &theme.StatusPalette.Warning.x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Restart needed", &theme.StatusPalette.RestartNeeded.x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Current hotkey", &theme.StatusPalette.CurrentHotkey.x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Success", &theme.StatusPalette.SuccessColor.x), dirty, commit);
		TrackContinuousEdit(ImGui::ColorEdit4("Info", &theme.StatusPalette.InfoColor.x), dirty, commit);

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Full ImGui palette")) {
			static std::string colorSearch;
			ui::DrawFeatureSearchBar(colorSearch);

			for (int i = 0; i < ImGuiCol_COUNT; ++i) {
				const char* name = ImGuiColorName(i);
				if (!ui::StringMatchesSearch(name, colorSearch))
					continue;
				TrackContinuousEdit(
					ImGui::ColorEdit4(name, &theme.FullPalette[static_cast<std::size_t>(i)].x),
					dirty,
					commit);
			}
		}

		if (commit && dirty) {
			dirty = false;
			menu.Save();
		}
	}
}
