#include "Menu/HomePageRenderer.h"

#include "Feature.h"
#include "Menu/Fonts.h"
#include "Menu/Menu.h"
#include "Menu/ThemeManager.h"
#include "Utils/UI.h"

#include <array>
#include <format>
#include <string>

#include <imgui.h>

#include <Windows.h>
#include <shellapi.h>

namespace
{
	using namespace cs;

	struct QuickLink
	{
		const char* label;
		const char* url;
		const char* tooltip;
		bool enabled;
	};

	constexpr std::array<QuickLink, 3> kQuickLinks = {
		QuickLink{ "Nexus Mods", "https://www.nexusmods.com/fallout4", "Not available yet", false },
		QuickLink{ "GitHub", "https://github.com/northaxosky/fallout4-community-shaders", "Report an issue or read the source", true },
		QuickLink{ "Discord", "https://discord.gg/communityshaders", "Coming soon", false }
	};

	struct FaqEntry
	{
		const char* question;
		const char* answer;
	};

	constexpr std::array<FaqEntry, 5> kFaqEntries = {
		FaqEntry{
			"Why is nothing enabled?",
			"Every feature ships disabled. Pick a feature in the list on the left and turn on its boot toggle, then restart the game." },
		FaqEntry{
			"Where are my settings stored?",
			"Defaults live in FO4CommunityShaders.toml, which is never rewritten. In-game changes go to FO4CommunityShaders.User.toml beside it." },
		FaqEntry{
			"A feature says it needs a restart.",
			"Boot toggles only take effect during startup, so the feature loads on the next launch." },
		FaqEntry{
			"How do I go back to defaults?",
			"Use the revert button in the bottom-right of a feature page, or delete FO4CommunityShaders.User.toml." },
		FaqEntry{
			"The menu will not open.",
			"END opens the settings menu and F10 toggles the performance overlay. Another mod may be consuming those keys." }
	};

	void OpenUrl(const char* a_url)
	{
		ShellExecuteA(nullptr, "open", a_url, nullptr, nullptr, SW_SHOWNORMAL);
	}
}

namespace cs
{
	void HomePageRenderer::RenderHomePage()
	{
		RenderWelcomeSection();
		RenderQuickLinksSection();
		RenderFAQSection();
	}

	void HomePageRenderer::RenderWelcomeSection()
	{
		{
			MenuFonts::FontRoleGuard titleGuard(Menu::FontRole::Title);
			ImGui::TextUnformatted("Welcome to Fallout 4 Community Shaders");
		}
		ImGui::Spacing();

		{
			MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
			ImGui::TextWrapped("%s",
				"Community Shaders adds modern rendering features to Fallout 4 without replacing the game's shaders wholesale. "
				"Pick a feature from the list on the left to configure it.");
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const auto& features = FeatureManager::Get().GetRegisteredFeatures();
		std::size_t installed = 0;
		std::size_t active = 0;
		for (const Feature* feature : features) {
			if (!feature)
				continue;
			if (feature->IsInstalled())
				++installed;
			if (feature->IsActive())
				++active;
		}

		const auto& statusPalette = Menu::Get().GetTheme().StatusPalette;
		ImGui::Text("Features installed: %zu of %zu", installed, features.size());
		ImGui::SameLine();
		ImGui::TextColored(active > 0 ? statusPalette.SuccessColor : statusPalette.Disable, "(%zu active)", active);

		ImGui::Spacing();
	}

	void HomePageRenderer::RenderQuickLinksSection()
	{
		ui::DrawSectionHeader("Quick Links", true, false);

		const float buttonWidth = ImGui::GetContentRegionAvail().x / static_cast<float>(kQuickLinks.size()) -
		                          ImGui::GetStyle().ItemSpacing.x;
		for (std::size_t i = 0; i < kQuickLinks.size(); ++i) {
			const auto& link = kQuickLinks[i];
			ImGui::BeginDisabled(!link.enabled);
			const bool clicked = ui::ButtonWithFlash(link.label, ImVec2(buttonWidth, 0));
			ImGui::EndDisabled();

			if (link.enabled && clicked)
				OpenUrl(link.url);

			const ImGuiHoveredFlags hoverFlags =
				ImGuiHoveredFlags_DelayNormal | (link.enabled ? ImGuiHoveredFlags_None : ImGuiHoveredFlags_AllowWhenDisabled);
			if (ImGui::IsItemHovered(hoverFlags)) {
				ImGui::BeginTooltip();
				ImGui::Text("%s", link.tooltip);
				ImGui::EndTooltip();
			}

			if (i + 1 < kQuickLinks.size())
				ImGui::SameLine();
		}

		ImGui::Spacing();
	}

	void HomePageRenderer::RenderFAQSection()
	{
		ui::DrawSectionHeader("FAQ", true, false);

		for (const auto& entry : kFaqEntries) {
			if (ImGui::CollapsingHeader(entry.question)) {
				MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
				ImGui::Indent();
				ImGui::TextWrapped("%s", entry.answer);
				ImGui::Unindent();
				ImGui::Spacing();
			}
		}
	}

	bool HomePageRenderer::ShouldShowFirstTimeSetup()
	{
		return !Menu::Get().GetSettings().FirstTimeSetupCompleted;
	}

	void HomePageRenderer::MarkFirstTimeSetupComplete()
	{
		auto& menu = Menu::Get();
		menu.GetSettings().FirstTimeSetupCompleted = true;
		menu.Save();
	}

	void HomePageRenderer::RenderFirstTimeSetupDialog()
	{
		if (!ShouldShowFirstTimeSetup())
			return;

		constexpr const char* popupId = "First-time setup###CSFirstTimeSetup";
		ImGui::OpenPopup(popupId);

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

		if (ui::BeginPopupModalWithRoundedClose(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextWrapped("%s",
				"Community Shaders is installed but every feature is off by default.\n\n"
				"Open a feature from the list on the left, switch on its boot toggle, then restart the game.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			const float buttonWidth = ThemeManager::Constants::POPUP_BUTTON_WIDTH * ui::GetUIScale();
			if (ImGui::Button("Got it", ImVec2(buttonWidth, 0))) {
				MarkFirstTimeSetupComplete();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}
