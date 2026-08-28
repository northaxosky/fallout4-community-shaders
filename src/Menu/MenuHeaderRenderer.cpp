#include "Menu/MenuHeaderRenderer.h"

#include "Log.h"
#include "Menu/Fonts.h"
#include "Menu/ThemeManager.h"
#include "Plugin.h"
#include "Settings/FeatureConfig.h"
#include "Utils/ShaderCache/CacheStorage.h"
#include "Utils/UI.h"

#include <filesystem>
#include <format>

#include <imgui.h>
#include <imgui_internal.h>

namespace
{
	auto* L = cs::log::Get("menu");

	using RoleFontGuard = cs::MenuFonts::FontRoleGuard;

	constexpr const char* kClearCacheTooltip =
		"Clears the on-disk shader cache. "
		"The cache holds compiled shader bytecode so features do not recompile on every launch. "
		"Clearing means shaders are recompiled the next time the game encounters them.";

	void ClearShaderCacheAction()
	{
		const auto root = cs::shader_cache::DefaultCacheRoot();
		std::error_code ec;
		const auto removed = std::filesystem::remove_all(root, ec);
		if (ec) {
			L->warn("Failed to clear the shader cache at '{}': {}", root.string(), ec.message());
			cs::Menu::ShowToast("Failed to clear the shader cache (see log)", 4.0);
			return;
		}
		L->info("Cleared {} shader cache entries from '{}'", removed, root.string());
		cs::Menu::ShowToast("Shader cache cleared", 2.5);
	}

	cs::ui::ConfirmationPopup& ClearCachePopup()
	{
		static cs::ui::ConfirmationPopup popup{
			"Clear Shader Cache",
			"This deletes every compiled shader record on disk. Shaders are recompiled the next time they are needed.",
			"Clear",
			"Cancel"
		};
		return popup;
	}
}

namespace cs
{
	void MenuHeaderRenderer::RenderHeader(bool a_isDocked, bool a_canShowIcons, float a_uiScale, const Menu::UIIcons& a_uiIcons)
	{
		auto& menu = Menu::Get();

		const auto title = ui::GetMenuDisplayTitle();
		const auto actionIcons = BuildActionIcons(a_canShowIcons, a_uiIcons);

		if (a_isDocked) {
			RenderDockedIcons(actionIcons, a_uiScale);
		} else {
			const bool centerHeader = menu.GetTheme().CenterHeader;

			if (a_canShowIcons && ImGui::BeginTable("##HeaderLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableNextColumn();

				const float textScaleFactor = ThemeManager::Constants::HEADER_BASE_TEXT_SCALE * a_uiScale;

				if (centerHeader) {
					float contentWidth = 0.0f;
					{
						RoleFontGuard titleFont(Menu::FontRole::Title);
						ImGui::SetWindowFontScale(textScaleFactor);
						contentWidth += ImGui::CalcTextSize(title.c_str()).x;
						ImGui::SetWindowFontScale(1.0f);
					}

					const float offset = ui::GetCenterOffsetForContent(contentWidth);
					if (offset > 0.0f)
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
				} else {
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ThemeManager::Constants::CURSOR_POSITION_PADDING);
				}

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					ui::DrawSharpText(title.c_str(), true, textScaleFactor);
				}
				ImGui::PopStyleVar();

				ImGui::TableNextColumn();
				RenderUndockedIcons(actionIcons, a_uiScale);

				ImGui::EndTable();
			} else if (!a_canShowIcons) {
				const float textScaleFactor = ThemeManager::Constants::HEADER_FALLBACK_TEXT_SCALE * a_uiScale;

				if (centerHeader) {
					float textWidth = 0.0f;
					{
						RoleFontGuard titleFont(Menu::FontRole::Title);
						ImGui::SetWindowFontScale(textScaleFactor);
						textWidth = ImGui::CalcTextSize(title.c_str()).x;
						ImGui::SetWindowFontScale(1.0f);
					}

					const float offset = ui::GetCenterOffsetForContent(textWidth);
					if (offset > 0.0f)
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
				}

				ImGui::SetWindowFontScale(textScaleFactor);
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					ImGui::TextUnformatted(title.c_str());
				}
				ImGui::SetWindowFontScale(1.0f);
			}
		}

		if (!a_isDocked) {
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
			ImGui::Spacing();
		}

		// Without icon textures the same actions appear as text buttons.
		if (!a_canShowIcons && !a_isDocked) {
			if (ui::ButtonWithFlash("Clear Shader Cache", { -1, 0 }))
				ClearCachePopup().Request();
			if (auto tooltip = ui::HoverTooltipWrapper())
				ImGui::Text("%s", kClearCacheTooltip);

			ImGui::Spacing();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
			ImGui::Spacing();
		}
	}

	std::vector<MenuHeaderRenderer::ActionIcon> MenuHeaderRenderer::BuildActionIcons(bool a_canShowIcons, const Menu::UIIcons& a_uiIcons)
	{
		std::vector<ActionIcon> actionIcons;

		if (!a_canShowIcons)
			return actionIcons;

		if (a_uiIcons.clearCache.texture)
			actionIcons.push_back({ a_uiIcons.clearCache.texture, kClearCacheTooltip, []() { ClearCachePopup().Request(); } });

		return actionIcons;
	}

	void MenuHeaderRenderer::RenderDockedIcons(const std::vector<ActionIcon>& a_actionIcons, float a_uiScale)
	{
		if (a_actionIcons.empty())
			return;

		const float currentFontSize = ImGui::GetFontSize();
		const float iconSize = currentFontSize * ThemeManager::Constants::DOCKED_ICON_SIZE_MULTIPLIER * a_uiScale;
		const float iconSpacing = ThemeManager::Constants::DOCKED_ICON_SPACING * a_uiScale;
		const float rightMargin = ThemeManager::Constants::DOCKED_RIGHT_MARGIN * a_uiScale;

		const ImVec2 windowPos = ImGui::GetWindowPos();
		const ImVec2 windowSize = ImGui::GetWindowSize();
		const float titleBarHeight = ImGui::GetFrameHeight();

		// Drawn on the foreground list because the title bar is outside the window content area.
		ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

		float iconX = windowPos.x + windowSize.x - rightMargin;
		const float iconY = windowPos.y + (titleBarHeight - iconSize) * 0.5f;

		const auto& theme = Menu::Get().GetTheme();
		for (auto it = a_actionIcons.rbegin(); it != a_actionIcons.rend(); ++it) {
			iconX -= iconSize + iconSpacing;

			const float paddingReduction = ThemeManager::Constants::DOCKED_ICON_PADDING_REDUCTION * a_uiScale;
			const ImVec2 iconMin(iconX + paddingReduction, iconY + paddingReduction);
			const ImVec2 iconMax(iconX + iconSize - paddingReduction, iconY + iconSize - paddingReduction);

			const ImVec2 interactionMin(iconX, iconY);
			const ImVec2 interactionMax(iconX + iconSize, iconY + iconSize);

			const bool isHovered = ImGui::IsMouseHoveringRect(interactionMin, interactionMax, false);
			ui::DrawRoundedButtonHighlight(interactionMin, interactionMax, isHovered,
				isHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left), fgDrawList);

			if (it->texture) {
				ImU32 tintColor;
				if (theme.UseMonochromeIcons) {
					ImVec4 textColor = theme.FullPalette[ImGuiCol_Text];
					if (!isHovered)
						textColor.w *= 0.85f;
					tintColor = ImGui::GetColorU32(textColor);
				} else {
					tintColor = isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 220);
				}
				fgDrawList->AddImage(it->texture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tintColor);
			}

			if (isHovered) {
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					it->callback();

				ImGui::SetTooltip("%s", it->tooltip);
			}
		}
	}

	void MenuHeaderRenderer::RenderUndockedIcons(const std::vector<ActionIcon>& a_actionIcons, float a_uiScale)
	{
		if (a_actionIcons.empty())
			return;

		const float currentFontSize = ImGui::GetFontSize();
		const float iconSize = currentFontSize * ThemeManager::Constants::HEADER_BASE_ICON_MULTIPLIER * a_uiScale;
		const float paddingReduction = ThemeManager::Constants::UNDOCKED_ICON_PADDING_REDUCTION * a_uiScale;
		const ImVec2 imageSize(iconSize - paddingReduction, iconSize - paddingReduction);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ThemeManager::Constants::UNDOCKED_ICON_ITEM_SPACING, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));

		const ImVec4 tintColor = ui::GetIconTint();

		for (std::size_t i = 0; i < a_actionIcons.size(); ++i) {
			const auto& icon = a_actionIcons[i];
			if (!icon.texture)
				continue;

			const std::string buttonId = std::format("##ActionBtn{}", i);
			if (ImGui::ImageButton(buttonId.c_str(), icon.texture, imageSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor))
				icon.callback();
			if (auto tooltip = ui::HoverTooltipWrapper())
				ImGui::Text("%s", icon.tooltip);

			if (i < a_actionIcons.size() - 1)
				ImGui::SameLine();
		}

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);
	}

	void MenuHeaderRenderer::DrawGlobalPopups()
	{
		if (ClearCachePopup().Draw())
			ClearShaderCacheAction();
	}
}
