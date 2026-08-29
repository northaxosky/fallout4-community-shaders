#include "Menu/FeatureListRenderer.h"

#include "Feature.h"
#include "FeatureCategories.h"
#include "Log.h"
#include "Menu/Fonts.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/ImGuiRecovery.h"
#include "Menu/Menu.h"
#include "Menu/ThemeManager.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <unordered_set>
#include <utility>

#include <shellapi.h>

#include <imgui.h>
#include <imgui_internal.h>

namespace
{
	auto* L = cs::log::Get("menu");

	using namespace cs;

	constexpr std::array<std::string_view, 4> CORE_MENU_NAMES = { "Home", "General", "Advanced", "Presets" };
	std::unordered_set<const Feature*> failedMenuCallbacks;

	bool IsCoreMenu(const std::string& a_name)
	{
		return std::find(CORE_MENU_NAMES.begin(), CORE_MENU_NAMES.end(), a_name) != CORE_MENU_NAMES.end();
	}

	template <class Callback>
	bool InvokeFeatureCallback(Feature& a_feature, std::string_view a_phase, bool a_requirePrepared, Callback&& a_callback)
	{
		if (failedMenuCallbacks.contains(&a_feature))
			return false;

		auto& featureManager = FeatureManager::Get();
		if (a_requirePrepared && !featureManager.PrepareMenuCallback(a_feature, a_phase))
			return false;

		auto recovery = ImGuiRecoverySnapshot::Capture();
		if (!recovery) {
			failedMenuCallbacks.insert(&a_feature);
			featureManager.QuarantineRuntimeCallback(a_feature, a_phase, "ImGui recovery snapshot unavailable");
			featureManager.FinishRuntimeCallbackPass();
			return false;
		}

		try {
			std::forward<Callback>(a_callback)();
			return true;
		} catch (const std::exception& e) {
			recovery->Recover();
			featureManager.QuarantineRuntimeCallback(a_feature, a_phase, e.what());
		} catch (...) {
			recovery->Recover();
			featureManager.QuarantineRuntimeCallback(a_feature, a_phase, "non-standard exception");
		}

		failedMenuCallbacks.insert(&a_feature);
		featureManager.FinishRuntimeCallbackPass();
		L->warn("Feature menu callback '{}' failed and was suppressed", a_phase);
		return false;
	}

	bool ShouldShowLeftPanel()
	{
		static bool leftPanelVisible = true;
		static float hoverStartTime = 0.0f;
		static bool wasHovering = false;

		if (!Menu::Get().GetSettings().AutoHideFeatureList) {
			leftPanelVisible = true;
			return true;
		}

		const ImVec2 mousePos = ImGui::GetMousePos();
		const ImVec2 windowPos = ImGui::GetWindowPos();
		const ImVec2 windowSize = ImGui::GetWindowSize();
		const float relativeX = mousePos.x - windowPos.x;
		// A full-height edge keeps the hidden panel recoverable.
		const bool inActivationZone =
			relativeX >= 0.0f && relativeX < ThemeManager::Constants::AUTOHIDE_ACTIVATION_ZONE_WIDTH;
		const bool overPanel =
			leftPanelVisible &&
			relativeX >= 0.0f &&
			relativeX < windowSize.x * ThemeManager::Constants::AUTOHIDE_PANEL_WIDTH_RATIO &&
			mousePos.y >= windowPos.y &&
			mousePos.y <= windowPos.y + windowSize.y;

		if (inActivationZone && !wasHovering) {
			hoverStartTime = static_cast<float>(ImGui::GetTime());
			wasHovering = true;
		} else if (!inActivationZone) {
			wasHovering = false;
		}

		const bool shouldExpand =
			inActivationZone &&
			static_cast<float>(ImGui::GetTime()) - hoverStartTime >= ThemeManager::Constants::AUTOHIDE_EXPAND_DELAY;
		if (shouldExpand || overPanel)
			leftPanelVisible = true;
		else if (!inActivationZone)
			leftPanelVisible = false;

		return leftPanelVisible;
	}

	void SeparatorTextWithFont(const std::string& a_label, Menu::FontRole a_role)
	{
		MenuFonts::FontRoleGuard fontGuard(a_role);
		ImGui::SeparatorText(a_label.c_str());
	}

	bool IsDisabledAtBoot(const Feature& a_feature)
	{
		return Menu::Get().IsFeatureDisabledAtBoot(a_feature);
	}

	bool ToggleAtBoot(const Feature& a_feature)
	{
		const bool desired = IsDisabledAtBoot(a_feature);
		if (!Menu::Get().SetFeatureLoadAtBoot(a_feature, desired))
			return !desired;
		return desired;
	}

	// Returns the title row height.
	float DrawFeatureHeader(const std::string& a_featureName, const std::string& a_description)
	{
		const auto& themeSettings = Menu::Get().GetTheme();

		// A malformed theme must not destabilize layout, so clamp to the slider range.
		float titleScale = themeSettings.FeatureHeading.FeatureTitleScale;
		if (!std::isfinite(titleScale))
			titleScale = ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE;
		titleScale = std::clamp(titleScale, 1.0f, 3.0f);

		const ImVec2 startPos = ImGui::GetCursorScreenPos();

		ImVec2 titleSize;
		{
			MenuFonts::FontRoleGuard titleGuard(Menu::FontRole::Title);
			titleSize = ImGui::CalcTextSize(a_featureName.c_str());
			titleSize.x *= titleScale;
			titleSize.y *= titleScale;

			ImGui::SetWindowFontScale(titleScale);
			ImGui::TextUnformatted(a_featureName.c_str());
			ImGui::SetWindowFontScale(1.0f);
		}

		const float titleOnlyHeight = titleSize.y;

		if (!a_description.empty()) {
			ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y + titleSize.y + ImGui::GetStyle().ItemSpacing.y * 0.25f));

			ImVec4 descriptionColor = themeSettings.FullPalette[ImGuiCol_Text];
			descriptionColor.w *= ThemeManager::Constants::VERSION_TEXT_OPACITY;

			MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
			ImGui::PushStyleColor(ImGuiCol_Text, descriptionColor);
			ImGui::TextWrapped("%s", a_description.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();

		return titleOnlyHeight;
	}
}

namespace cs
{
	void FeatureListRenderer::RenderFeatureList(
		float a_footerHeight,
		std::size_t& a_selectedMenu,
		std::string& a_featureSearch,
		std::string& a_pendingFeatureSelection,
		std::map<std::string, bool>& a_categoryExpansionStates,
		const std::function<void()>& a_drawGeneralSettings,
		const std::function<void()>& a_drawAdvancedSettings,
		const std::function<void()>& a_drawPresets)
	{
		ImGui::BeginChild("Menus Table", ImVec2(0, -a_footerHeight));

		const auto menuList = BuildMenuList(a_featureSearch, a_categoryExpansionStates,
			a_drawGeneralSettings, a_drawAdvancedSettings, a_drawPresets);

		HandlePendingFeatureSelection(a_pendingFeatureSelection, menuList, a_selectedMenu);

		const bool leftPanelVisible = ShouldShowLeftPanel();
		const int numColumns = leftPanelVisible ? 2 : 1;
		if (ImGui::BeginTable("Menus Table", numColumns, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
			if (leftPanelVisible) {
				ImGui::TableSetupColumn("##ListOfMenus", 0, 2);
				ImGui::TableSetupColumn("##MenuConfig", 0, 8);
				RenderLeftColumn(menuList, a_selectedMenu, a_featureSearch, a_categoryExpansionStates);
				RenderRightColumn(menuList, a_selectedMenu, a_pendingFeatureSelection);
			} else {
				ImGui::TableSetupColumn("##MenuConfig", 0, 1);
				RenderRightColumn(menuList, a_selectedMenu, a_pendingFeatureSelection);
			}

			ImGui::EndTable();
		}

		ImGui::EndChild();
	}

	std::vector<FeatureListRenderer::MenuFuncInfo> FeatureListRenderer::BuildMenuList(
		const std::string& a_featureSearch,
		std::map<std::string, bool>& a_categoryExpansionStates,
		const std::function<void()>& a_drawGeneralSettings,
		const std::function<void()>& a_drawAdvancedSettings,
		const std::function<void()>& a_drawPresets)
	{
		auto sortedFeatureList = FeatureManager::Get().GetRegisteredFeatures();
		std::ranges::sort(sortedFeatureList, [](const Feature* a_lhs, const Feature* a_rhs) {
			return a_lhs->GetDisplayName() < a_rhs->GetDisplayName();
		});

		if (!a_featureSearch.empty()) {
			const auto removed = std::remove_if(sortedFeatureList.begin(), sortedFeatureList.end(),
				[&a_featureSearch](const Feature* a_feature) { return !ui::FeatureMatchesSearch(a_feature, a_featureSearch); });
			sortedFeatureList.erase(removed, sortedFeatureList.end());
		}

		// Rebuilding preserves caller-owned expansion state.
		auto menuList = std::vector<MenuFuncInfo>{
			BuiltInMenu{ "Home", []() { HomePageRenderer::RenderHomePage(); } },
			BuiltInMenu{ "General", a_drawGeneralSettings },
			BuiltInMenu{ "Advanced", a_drawAdvancedSettings },
			BuiltInMenu{ "Presets", a_drawPresets }
		};

		std::map<std::string, std::vector<Feature*>> categorizedFeatures;
		for (Feature* feature : sortedFeatureList) {
			if (feature->IsInMenu() && feature->IsActive())
				categorizedFeatures[feature->GetCategory()].push_back(feature);
		}

		for (auto& [category, features] : categorizedFeatures) {
			std::ranges::sort(features, [](const Feature* a_lhs, const Feature* a_rhs) {
				return a_lhs->GetDisplayName() < a_rhs->GetDisplayName();
			});
		}

		const std::vector<std::string> categoryOrder = {
			FeatureCategories::kLighting,
			FeatureCategories::kPostProcess,
			FeatureCategories::kCompatibility,
			FeatureCategories::kPerformance,
			FeatureCategories::kDevTools,
			FeatureCategories::kMisc
		};

		auto appendCategory = [&](const std::string& a_category, const std::vector<Feature*>& a_features) {
			if (a_features.empty())
				return;
			if (!a_categoryExpansionStates.contains(a_category))
				a_categoryExpansionStates[a_category] = true;

			menuList.push_back(CategoryHeader{ a_category });
			if (a_categoryExpansionStates[a_category])
				std::ranges::copy(a_features, std::back_inserter(menuList));
		};

		for (const std::string& category : categoryOrder) {
			const auto it = categorizedFeatures.find(category);
			if (it != categorizedFeatures.end())
				appendCategory(category, it->second);
		}

		for (const auto& [category, features] : categorizedFeatures) {
			if (std::find(categoryOrder.begin(), categoryOrder.end(), category) == categoryOrder.end())
				appendCategory(category, features);
		}

		std::vector<Feature*> unloadedFeatures;
		for (Feature* feature : sortedFeatureList) {
			if (!feature->IsActive() && feature->IsInMenu())
				unloadedFeatures.push_back(feature);
		}
		if (!unloadedFeatures.empty()) {
			menuList.push_back(std::string("Unloaded Features"));
			std::ranges::copy(unloadedFeatures, std::back_inserter(menuList));
		}

		return menuList;
	}

	void FeatureListRenderer::HandlePendingFeatureSelection(
		std::string& a_pendingFeatureSelection,
		const std::vector<MenuFuncInfo>& a_menuList,
		std::size_t& a_selectedMenu)
	{
		if (a_pendingFeatureSelection.empty())
			return;

		for (std::size_t i = 0; i < a_menuList.size(); ++i) {
			if (!std::holds_alternative<Feature*>(a_menuList[i]))
				continue;

			const Feature* feature = std::get<Feature*>(a_menuList[i]);
			if (feature->GetName() == a_pendingFeatureSelection) {
				a_selectedMenu = i;
				L->info("Navigated to the {} feature menu", a_pendingFeatureSelection);
				break;
			}
		}
		a_pendingFeatureSelection.clear();
	}

	void FeatureListRenderer::RenderLeftColumn(
		const std::vector<MenuFuncInfo>& a_menuList,
		std::size_t& a_selectedMenu,
		std::string& a_featureSearch,
		std::map<std::string, bool>& a_categoryExpansionStates)
	{
		ImGui::TableNextColumn();
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
		if (ImGui::BeginListBox("##MenusList", { -FLT_MIN, -FLT_MIN })) {
			std::size_t renderedCoreMenus = 0;
			for (std::size_t i = 0; i < a_menuList.size() && renderedCoreMenus < CORE_MENU_NAMES.size(); ++i) {
				if (!std::holds_alternative<BuiltInMenu>(a_menuList[i]))
					continue;
				if (!IsCoreMenu(std::get<BuiltInMenu>(a_menuList[i]).name))
					continue;

				std::visit(ListMenuVisitor{ i, a_selectedMenu, a_categoryExpansionStates }, a_menuList[i]);
				++renderedCoreMenus;
			}

			ui::DrawSectionHeader("Features", true);
			ui::DrawFeatureSearchBar(a_featureSearch);

			for (std::size_t i = 0; i < a_menuList.size(); ++i) {
				if (std::holds_alternative<BuiltInMenu>(a_menuList[i]) && IsCoreMenu(std::get<BuiltInMenu>(a_menuList[i]).name))
					continue;
				std::visit(ListMenuVisitor{ i, a_selectedMenu, a_categoryExpansionStates }, a_menuList[i]);
			}

			ImGui::EndListBox();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}

	void FeatureListRenderer::RenderRightColumn(
		const std::vector<MenuFuncInfo>& a_menuList,
		std::size_t a_selectedMenu,
		std::string& a_pendingFeatureSelection)
	{
		ImGui::TableNextColumn();

		if (a_selectedMenu < a_menuList.size())
			std::visit(DrawMenuVisitor{ a_pendingFeatureSelection }, a_menuList[a_selectedMenu]);
		else
			ImGui::TextDisabled("Please select an item on the left.");
	}

	void FeatureListRenderer::ListMenuVisitor::operator()(const BuiltInMenu& a_menu)
	{
		MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Subheading);

		if (ImGui::Selectable(std::format(" {} ", a_menu.name).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns))
			selectedMenuRef = listId;
	}

	void FeatureListRenderer::ListMenuVisitor::operator()(const std::string& a_label)
	{
		if (a_label == "Unloaded Features")
			ui::DrawSectionHeader(a_label.c_str(), true);
		else
			SeparatorTextWithFont(a_label, Menu::FontRole::Subheading);
	}

	void FeatureListRenderer::ListMenuVisitor::operator()(const CategoryHeader& a_header)
	{
		bool isExpanded = categoryExpansionStates[a_header.name];

		{
			MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Heading);
			const auto& counts = Menu::GetCategoryCounts();
			const auto it = counts.find(a_header.name);
			const int count = it != counts.end() ? it->second : 0;
			ui::DrawCategoryHeader(a_header.name.c_str(), a_header.name.c_str(), isExpanded, count);
		}

		categoryExpansionStates[a_header.name] = isExpanded;
	}

	void FeatureListRenderer::ListMenuVisitor::operator()(Feature* a_feature)
	{
		MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Subheading);

		const auto& themeSettings = Menu::Get().GetTheme();
		const auto& state = a_feature->GetState();
		const bool isDisabled = IsDisabledAtBoot(*a_feature);
		const bool isActive = a_feature->IsActive();

		ImVec4 textColor;
		if (isDisabled)
			textColor = themeSettings.StatusPalette.Disable;
		else if (isActive)
			textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		else if (state.runtimeState == FeatureRuntimeState::kFailed)
			textColor = themeSettings.StatusPalette.Error;
		else if (state.runtimeState == FeatureRuntimeState::kDegraded)
			textColor = themeSettings.StatusPalette.Warning;
		else
			// Installed inactive features await restart.
			textColor = a_feature->IsInstalled() ? themeSettings.StatusPalette.RestartNeeded : themeSettings.StatusPalette.Disable;

		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		if (ImGui::Selectable(std::format(" {} ", a_feature->GetDisplayName()).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns))
			selectedMenuRef = listId;
		ImGui::PopStyleColor();

		if (state.runtimeState == FeatureRuntimeState::kDegraded) {
			ImGui::SameLine();
			ImGui::TextColored(themeSettings.StatusPalette.Warning, "(degraded)");
		}
	}

	void FeatureListRenderer::DrawMenuVisitor::operator()(const BuiltInMenu& a_menu)
	{
		if (ImGui::BeginChild("##FeatureConfigFrame", { 0, 0 }, true)) {
			if (a_menu.name == "Home")
				ImGui::Dummy(ImVec2(0, ThemeManager::Constants::BUTTON_SPACING));
			a_menu.func();
		}
		ImGui::EndChild();
	}

	void FeatureListRenderer::DrawMenuVisitor::operator()(const std::string&)
	{
		// Labels are not selectable.
	}

	void FeatureListRenderer::DrawMenuVisitor::operator()(const CategoryHeader&)
	{
		ImGui::TextDisabled("Please select a feature from the left.");
	}

	void FeatureListRenderer::DrawMenuVisitor::operator()(Feature* a_feature)
	{
		if (ImGui::BeginChild("##FeatureConfigFrame", { 0, 0 }, true))
			RenderFeatureContent(*a_feature);
		ImGui::EndChild();
	}

	void FeatureListRenderer::RenderFeatureContent(Feature& a_feature)
	{
		const bool isDisabled = IsDisabledAtBoot(a_feature);
		const bool isActive = a_feature.IsActive();

		RenderFeatureHeader(&a_feature, isDisabled);
		RenderFeatureSettings(&a_feature, isDisabled, isActive);
		RenderRestoreDefaultsButton(&a_feature, isDisabled, isActive);
	}

	void FeatureListRenderer::RenderFeatureHeader(Feature* a_feature, bool a_isDisabled)
	{
		const auto& themeSettings = Menu::Get().GetTheme();

		const float bootToggleWidth = ImGui::GetFrameHeight() * 1.6f;
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 titleStartPos = ImGui::GetCursorScreenPos();

		std::string summary;
		InvokeFeatureCallback(*a_feature, "Menu::GetFeatureSummary", false, [&]() {
			summary = a_feature->GetFeatureSummary();
		});
		const float titleOnlyHeight = DrawFeatureHeader(std::string(a_feature->GetDisplayName()), summary);
		const ImVec2 cursorPosAfterHeader = ImGui::GetCursorScreenPos();

		const float buttonHeight = ImGui::GetFrameHeight();
		const float buttonY = titleStartPos.y + (titleOnlyHeight - buttonHeight) * 0.5f;
		ImGui::SetCursorScreenPos(ImVec2(titleStartPos.x + availableWidth - bootToggleWidth, buttonY));

		bool bootEnabled = !a_isDisabled;
		const bool failed = a_feature->GetState().runtimeState == FeatureRuntimeState::kFailed;
		if (failed)
			ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Error);

		if (ui::FeatureToggle("##BootToggle", &bootEnabled)) {
			const bool newState = ToggleAtBoot(*a_feature);
			L->info("{}: {} at boot.", a_feature->GetName(), newState ? "enabled" : "disabled");
		}

		if (failed)
			ImGui::PopStyleColor();

		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text(
				"Toggle feature loading at boot.\n"
				"Current state: %s\n"
				"Restart required for changes to take effect.\n"
				"Disabling removes performance impact.",
				bootEnabled ? "Enabled" : "Disabled");
		}

		ImGui::SetCursorScreenPos(cursorPosAfterHeader);
	}

	void FeatureListRenderer::RenderFeatureSettings(Feature* a_feature, bool a_isDisabled, bool a_isActive)
	{
		const auto& themeSettings = Menu::Get().GetTheme();

		if (a_isDisabled) {
			ImGui::TextColored(themeSettings.StatusPalette.Disable, "%s",
				"Feature settings are hidden because this feature is disabled at boot.");
			ImGui::Spacing();
			ImGui::Text("%s", "Enable the feature above to access its configuration options.");
			return;
		}

		const auto& state = a_feature->GetState();
		if (state.runtimeState != FeatureRuntimeState::kFailed &&
			state.runtimeState != FeatureRuntimeState::kDegraded &&
			a_isActive) {
			const ImVec2 cursorPosBefore = ImGui::GetCursorPos();
			InvokeFeatureCallback(*a_feature, "Menu::DrawSettings", true, [&]() {
				CS_FEATURE_ZONE(a_feature, "DrawSettings");
				a_feature->DrawSettings();
			});
			const ImVec2 cursorPosAfter = ImGui::GetCursorPos();

			constexpr float cursorEpsilon = 0.1f;
			const bool cursorMoved = std::abs(cursorPosAfter.x - cursorPosBefore.x) > cursorEpsilon ||
			                         std::abs(cursorPosAfter.y - cursorPosBefore.y) > cursorEpsilon;
			if (!cursorMoved)
				ImGui::TextColored(themeSettings.StatusPalette.Disable, "%s", "There are no settings available for this feature.");
			RenderRestartSettings(a_feature);
		} else if (state.runtimeState != FeatureRuntimeState::kFailed &&
				   state.runtimeState != FeatureRuntimeState::kDegraded) {
			if (a_feature->IsInstalled()) {
				ImGui::Text("%s", "This feature will be available after restart.");
			} else {
				InvokeFeatureCallback(*a_feature, "Menu::DrawUnloadedUI", false, [&]() {
					a_feature->DrawUnloadedUI();
				});
				std::optional<std::string> modLink;
				InvokeFeatureCallback(*a_feature, "Menu::GetFeatureModLink", false, [&]() {
					modLink = a_feature->GetFeatureModLink();
				});
				if (modLink && !modLink->empty()) {
					ImGui::Spacing();
					if (ImGui::Selectable(std::format("Click here to download this feature ({})", *modLink).c_str()))
						ShellExecuteA(nullptr, "open", modLink->c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					if (auto tooltip = ui::HoverTooltipWrapper())
						ImGui::Text("%s", "Download the feature from the mod page.");
				}
			}
		}

		if (state.runtimeState == FeatureRuntimeState::kFailed || state.runtimeState == FeatureRuntimeState::kDegraded) {
			ImGui::Spacing();
			SeparatorTextWithFont(state.runtimeState == FeatureRuntimeState::kFailed ? "Error" : "Warning", Menu::FontRole::Subheading);
			const auto& color = state.runtimeState == FeatureRuntimeState::kFailed ?
			                        themeSettings.StatusPalette.Error :
			                        themeSettings.StatusPalette.Warning;
			ImGui::TextColored(color, "%s", state.detail.empty() ? "See the log for details." : state.detail.c_str());
			InvokeFeatureCallback(*a_feature, "Menu::DrawFailLoadMessage", false, [&]() {
				a_feature->DrawFailLoadMessage();
			});
		}
	}

	void FeatureListRenderer::RenderRestartSettings(Feature* a_feature)
	{
		settings::RestartSettingsView restartSettings;
		if (!InvokeFeatureCallback(*a_feature, "Menu::GetRestartSettings", true, [&]() {
				restartSettings = a_feature->GetRestartSettings();
			}))
			return;

		bool drewHeader = false;
		for (const auto& field : restartSettings.fields) {
			if (!restartSettings.IsRestartRequired(field))
				continue;

			if (!drewHeader) {
				ImGui::Spacing();
				SeparatorTextWithFont("Restart Required", Menu::FontRole::Subheading);
				ImGui::TextColored(
					Menu::Get().GetTheme().StatusPalette.RestartNeeded,
					"Restart the game to apply:");
				drewHeader = true;
			}
			ImGui::BulletText("%.*s", static_cast<int>(field.label.size()), field.label.data());
		}
	}

	void FeatureListRenderer::RenderRestoreDefaultsButton(Feature* a_feature, bool a_isDisabled, bool a_isActive)
	{
		bool hasResettableSettings = false;
		if (a_isDisabled || !a_isActive ||
			!InvokeFeatureCallback(*a_feature, "Menu::HasResettableSettings", true, [&]() {
				hasResettableSettings = a_feature->HasResettableSettings();
			}) ||
			!hasResettableSettings)
			return;

		const auto& style = ImGui::GetStyle();
		const ImVec2 windowPos = ImGui::GetWindowPos();
		const ImVec2 windowSize = ImGui::GetWindowSize();
		const float scrollbarWidth = ImGui::GetScrollMaxY() > 0 ? style.ScrollbarSize : 0.0f;
		const float iconDimension = ImGui::GetFrameHeight() * 1.2f;
		const ImVec2 iconSize(iconDimension, iconDimension);
		const ImVec2 frameSize(iconSize.x + style.FramePadding.x * 2, iconSize.y + style.FramePadding.y * 2);
		ImGui::SetCursorScreenPos(ImVec2(
			windowPos.x + windowSize.x - frameSize.x - style.WindowPadding.x - scrollbarWidth,
			windowPos.y + windowSize.y - frameSize.y - style.WindowPadding.y));

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.3f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));

		auto& menu = Menu::Get();
		if (menu.uiIcons.featureSettingRevert.texture) {
			if (ImGui::ImageButton("##RestoreDefaults", menu.uiIcons.featureSettingRevert.texture, iconSize))
				InvokeFeatureCallback(*a_feature, "Menu::RestoreDefaultSettings", true, [&]() {
					a_feature->RestoreDefaultSettings();
				});
		} else {
			if (ImGui::Button("R##RestoreDefaults", iconSize))
				InvokeFeatureCallback(*a_feature, "Menu::RestoreDefaultSettings", true, [&]() {
					a_feature->RestoreDefaultSettings();
				});
		}

		ImGui::PopStyleColor(3);

		if (auto tooltip = ui::HoverTooltipWrapper())
			ImGui::Text("%s", "Restore default settings for this feature");
	}
}
