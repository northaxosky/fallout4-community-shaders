#include "Utils/UI.h"

#include "Feature.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Plugin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <format>
#include <mutex>
#include <numbers>
#include <unordered_map>

#include <imgui_internal.h>

#include <DirectXTex.h>
#include <WICTextureLoader.h>

namespace
{
	ImVec4 AdjustButtonColor(const ImVec4& a_color, float a_amount)
	{
		const float maxChannel = std::max({ a_color.x, a_color.y, a_color.z });
		constexpr float minChannel = cs::ThemeManager::Constants::BUTTON_MIN_COLOR_CHANNEL;
		constexpr float maxColorChannel = cs::ThemeManager::Constants::BUTTON_MAX_COLOR_CHANNEL;
		const float adjustment = maxChannel <= (maxColorChannel - a_amount) ? a_amount : -a_amount;
		return ImVec4(
			std::clamp(a_color.x + adjustment, minChannel, maxColorChannel),
			std::clamp(a_color.y + adjustment, minChannel, maxColorChannel),
			std::clamp(a_color.z + adjustment, minChannel, maxColorChannel),
			a_color.w);
	}

	float GetPillRounding(const ImVec2& a_min, const ImVec2& a_max)
	{
		return ImMin(a_max.x - a_min.x, a_max.y - a_min.y) * 0.5f;
	}

	float GetThemedButtonHighlightRounding(const ImVec2& a_min, const ImVec2& a_max)
	{
		const float frameRounding = ImGui::GetStyle().FrameRounding;
		return ImMin(ImMax(frameRounding, 0.0f), GetPillRounding(a_min, a_max));
	}

	constexpr float kTitleBarButtonPadding = 2.0f;
	constexpr float kCloseCrossDiagonalScale = 0.5f / std::numbers::sqrt2_v<float>;
	constexpr float kCloseCrossInset = 1.0f;
	constexpr ImVec4 kTransparentButtonChrome(0, 0, 0, 0);

	ImRect TitleBarButtonRect(const ImVec2& a_origin, float a_fontSize)
	{
		const float full = a_fontSize + kTitleBarButtonPadding * 2.0f;
		return ImRect(a_origin, ImVec2(a_origin.x + full, a_origin.y + full));
	}

	ImVec2 RightTitleBarButtonOrigin(ImGuiWindow* a_window, float a_fontSize, float a_offset = 0.0f)
	{
		const auto& style = ImGui::GetStyle();
		return ImVec2(
			a_window->Rect().Max.x - a_window->WindowBorderSize - style.FramePadding.x - a_fontSize - a_offset - kTitleBarButtonPadding,
			a_window->Rect().Min.y + style.FramePadding.y - kTitleBarButtonPadding);
	}

	ImVec2 CollapseTitleBarButtonOrigin(ImGuiWindow* a_window, bool a_hasCloseButton, float a_fontSize)
	{
		const auto& style = ImGui::GetStyle();
		if (style.WindowMenuButtonPosition == ImGuiDir_Right)
			return RightTitleBarButtonOrigin(a_window, a_fontSize, a_hasCloseButton ? a_fontSize : 0.0f);

		return ImVec2(
			a_window->Pos.x + a_window->WindowBorderSize + style.FramePadding.x - kTitleBarButtonPadding,
			a_window->Pos.y + style.FramePadding.y - kTitleBarButtonPadding);
	}

	bool IsTitleBarButtonHovered(ImGuiWindow* a_window, const ImRect& a_bb)
	{
		ImGuiContext& g = *ImGui::GetCurrentContext();
		return g.HoveredWindow == a_window && ImGui::IsMouseHoveringRect(a_bb.Min, a_bb.Max, false);
	}

	class NativeTitleBarButtonHighlightGuard
	{
	public:
		NativeTitleBarButtonHighlightGuard()
		{
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kTransparentButtonChrome);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, kTransparentButtonChrome);
		}

		~NativeTitleBarButtonHighlightGuard() { ImGui::PopStyleColor(2); }
		NativeTitleBarButtonHighlightGuard(const NativeTitleBarButtonHighlightGuard&) = delete;
		NativeTitleBarButtonHighlightGuard& operator=(const NativeTitleBarButtonHighlightGuard&) = delete;
	};

	void DrawRoundedCloseHighlight(ImGuiWindow* a_window)
	{
		if (a_window->Flags & ImGuiWindowFlags_NoTitleBar)
			return;

		const float sz = ImGui::GetFontSize();
		const ImVec2 pos = RightTitleBarButtonOrigin(a_window, sz);
		const ImRect bb = TitleBarButtonRect(pos, sz);
		const bool hovered = IsTitleBarButtonHovered(a_window, bb);
		const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

		a_window->DrawList->PushClipRect(a_window->Rect().Min, a_window->Rect().Max);
		const bool highlighted = cs::ui::DrawRoundedButtonHighlight(bb.Min, bb.Max, hovered, held, a_window->DrawList);

		// Cross geometry matches ImGui's internal RenderCloseButton.
		if (highlighted) {
			const ImVec2 c = bb.GetCenter();
			const float d = sz * kCloseCrossDiagonalScale - kCloseCrossInset;
			const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
			a_window->DrawList->AddLine({ c.x - d, c.y - d }, { c.x + d, c.y + d }, col);
			a_window->DrawList->AddLine({ c.x + d, c.y - d }, { c.x - d, c.y + d }, col);
		}
		a_window->DrawList->PopClipRect();
	}

	void DrawRoundedCollapseHighlight(ImGuiWindow* a_window, bool a_hasCloseButton)
	{
		if (a_window->Flags & ImGuiWindowFlags_NoTitleBar)
			return;
		if (a_window->Flags & ImGuiWindowFlags_NoCollapse)
			return;
		if (ImGui::GetStyle().WindowMenuButtonPosition == ImGuiDir_None)
			return;

		const float sz = ImGui::GetFontSize();
		const ImVec2 pos = CollapseTitleBarButtonOrigin(a_window, a_hasCloseButton, sz);
		const ImRect bb = TitleBarButtonRect(pos, sz);
		const bool hovered = IsTitleBarButtonHovered(a_window, bb);
		const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

		a_window->DrawList->PushClipRect(a_window->Rect().Min, a_window->Rect().Max);
		const bool highlighted = cs::ui::DrawRoundedButtonHighlight(bb.Min, bb.Max, hovered, held, a_window->DrawList);

		if (highlighted) {
			const ImVec2 arrowPos(pos.x + kTitleBarButtonPadding, pos.y + kTitleBarButtonPadding);
			const ImGuiDir dir = a_window->Collapsed ? ImGuiDir_Right : ImGuiDir_Down;
			ImGui::RenderArrow(a_window->DrawList, arrowPos, ImGui::GetColorU32(ImGuiCol_Text), dir, 1.0f);
		}

		a_window->DrawList->PopClipRect();
	}

	void DrawRoundedTitleBarButtonHighlights(ImGuiWindow* a_window, bool a_hasCloseButton, bool a_hasCollapseButton)
	{
		if (!a_window)
			return;

		if (a_hasCollapseButton)
			DrawRoundedCollapseHighlight(a_window, a_hasCloseButton);
		if (a_hasCloseButton)
			DrawRoundedCloseHighlight(a_window);
	}

	struct ComboSearchState
	{
		char buffer[256] = {};
		bool needsFocus = true;
	};

	std::unordered_map<std::string, ComboSearchState>& GetComboSearchStates()
	{
		static std::unordered_map<std::string, ComboSearchState> states;
		return states;
	}

	std::string ToLower(std::string_view a_text)
	{
		std::string lowered(a_text);
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](unsigned char a_ch) { return static_cast<char>(std::tolower(a_ch)); });
		return lowered;
	}
}

namespace cs::ui
{
	namespace paths
	{
		const std::filesystem::path& GetPluginPath()
		{
			static const std::filesystem::path path{ "Data\\F4SE\\Plugins\\FO4CommunityShaders" };
			return path;
		}

		std::filesystem::path GetFontsPath() { return GetPluginPath() / "Fonts"; }
		std::filesystem::path GetThemesPath() { return GetPluginPath() / "Themes"; }
		std::filesystem::path GetIconsPath() { return GetPluginPath() / "Icons"; }
		std::filesystem::path GetImGuiIniPath() { return GetPluginPath() / "imgui.ini"; }

		bool IsPathWithinDirectory(const std::filesystem::path& a_basePath, const std::filesystem::path& a_testPath)
		{
			std::error_code ec;
			const auto base = std::filesystem::weakly_canonical(a_basePath, ec);
			if (ec)
				return false;
			const auto test = std::filesystem::weakly_canonical(a_testPath, ec);
			if (ec)
				return false;

			auto baseIt = base.begin();
			auto testIt = test.begin();
			for (; baseIt != base.end(); ++baseIt, ++testIt) {
				if (testIt == test.end() || *baseIt != *testIt)
					return false;
			}
			return true;
		}
	}

	HoverTooltipWrapper::HoverTooltipWrapper() :
		hovered(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
	{
		if (hovered)
			ImGui::BeginTooltip();
	}

	HoverTooltipWrapper::~HoverTooltipWrapper()
	{
		if (hovered)
			ImGui::EndTooltip();
	}

	CenteredPopupModal::CenteredPopupModal(const char* a_name, bool* a_open, ImGuiWindowFlags a_flags, ImVec2 a_pos, ImVec2 a_pivot)
	{
		if (a_pos.x == kPopupCenter.x && a_pos.y == kPopupCenter.y) {
			const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		} else {
			ImGui::SetNextWindowPos(a_pos, ImGuiCond_Always, a_pivot);
		}
		isOpen = BeginPopupModalWithRoundedClose(a_name, a_open, a_flags);
	}

	CenteredPopupModal::~CenteredPopupModal()
	{
		if (isOpen)
			ImGui::EndPopup();
	}

	DisableGuard::DisableGuard(bool a_disable) :
		disable(a_disable)
	{
		if (disable)
			ImGui::BeginDisabled();
	}

	DisableGuard::~DisableGuard()
	{
		if (disable)
			ImGui::EndDisabled();
	}

	StyledButtonWrapper::StyledButtonWrapper(const ImVec4& a_normal, const ImVec4& a_hovered, const ImVec4& a_active) :
		m_pushedStyles(0)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, a_normal);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, a_hovered);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, a_active);
		m_pushedStyles = 3;
	}

	StyledButtonWrapper::StyledButtonWrapper(StyledButtonWrapper&& a_other) noexcept :
		m_pushedStyles(a_other.m_pushedStyles)
	{
		a_other.m_pushedStyles = 0;
	}

	StyledButtonWrapper::~StyledButtonWrapper()
	{
		if (m_pushedStyles > 0)
			ImGui::PopStyleColor(m_pushedStyles);
	}

	void ConfirmationPopup::Request()
	{
		if (dontAskAgainPersist && *dontAskAgainPersist)
			return;
		show = true;
		dontAskCheckbox = false;
	}

	bool ConfirmationPopup::Draw()
	{
		if (!show)
			return false;

		const char* popupId = title.empty() ? "Confirm##cs_confirm" : title.c_str();
		ImGui::OpenPopup(popupId);

		bool confirmed = false;
		if (auto popup = CenteredPopupModal(popupId)) {
			ImGui::TextWrapped("%s", message.c_str());
			ImGui::Spacing();

			if (showDontAskAgain) {
				ImGui::Checkbox("Don't ask me again", &dontAskCheckbox);
				ImGui::Spacing();
			}

			const float buttonWidth = ThemeManager::Constants::POPUP_BUTTON_WIDTH * GetUIScale();
			if (ImGui::Button(confirmLabel.c_str(), ImVec2(buttonWidth, 0))) {
				confirmed = true;
				if (dontAskAgainPersist && dontAskCheckbox)
					*dontAskAgainPersist = true;
				show = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(cancelLabel.c_str(), ImVec2(buttonWidth, 0))) {
				show = false;
				ImGui::CloseCurrentPopup();
			}
		}

		return confirmed;
	}

	StyledButtonWrapper StatusButtonStyle(const ImVec4& a_color)
	{
		const auto hover = AdjustButtonColor(a_color, ThemeManager::Constants::BUTTON_HOVER_BRIGHTEN);
		const auto active = AdjustButtonColor(a_color, ThemeManager::Constants::BUTTON_ACTIVE_BRIGHTEN);
		return StyledButtonWrapper(a_color, hover, active);
	}

	StyledButtonWrapper DestructiveButtonStyle()
	{
		return StatusButtonStyle(Menu::Get().GetTheme().StatusPalette.Error);
	}

	ImVec4 GetIconTint()
	{
		const auto& theme = Menu::Get().GetTheme();
		return theme.UseMonochromeIcons ? theme.FullPalette[ImGuiCol_Text] : ImVec4(1, 1, 1, 1);
	}

	bool ButtonWithFlash(const char* a_label, const ImVec2& a_size, int a_flashDurationMs)
	{
		static std::unordered_map<std::string, std::chrono::steady_clock::time_point> flashTimers;
		static std::mutex flashTimersMutex;

		const std::string buttonId(a_label);
		const auto now = std::chrono::steady_clock::now();

		bool hasActiveFlash = false;
		{
			std::lock_guard lock(flashTimersMutex);
			const auto it = flashTimers.find(buttonId);
			if (it != flashTimers.end()) {
				const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
				if (elapsed.count() < a_flashDurationMs)
					hasActiveFlash = true;
				else
					flashTimers.erase(it);
			}
		}

		bool styleChanged = false;
		if (hasActiveFlash) {
			const ImVec4 normalButton = ImGui::GetStyleColorVec4(ImGuiCol_Button);
			const ImVec4 flashColor(normalButton.x + 0.2f, normalButton.y + 0.2f, normalButton.z + 0.2f, normalButton.w);
			const ImVec4 flashHovered(flashColor.x * 1.1f, flashColor.y * 1.1f, flashColor.z * 1.1f, flashColor.w);
			const ImVec4 flashActive(flashColor.x * 0.9f, flashColor.y * 0.9f, flashColor.z * 0.9f, flashColor.w);

			ImGui::PushStyleColor(ImGuiCol_Button, flashColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, flashHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, flashActive);
			styleChanged = true;
		}

		const bool clicked = ImGui::Button(a_label, a_size);

		if (styleChanged)
			ImGui::PopStyleColor(3);

		if (clicked) {
			std::lock_guard lock(flashTimersMutex);
			flashTimers[buttonId] = now;
		}

		return clicked;
	}

	bool ErrorButton(const char* a_label, const ImVec2& a_size)
	{
		auto style = DestructiveButtonStyle();
		return ImGui::Button(a_label, a_size);
	}

	bool FeatureToggle(const char* a_label, bool* a_enabled, const ImVec2& a_size)
	{
		if (!a_enabled)
			return false;

		ImVec2 toggleSize = a_size;
		if (toggleSize.x <= 0)
			toggleSize.x = ImGui::GetFrameHeight() * 1.6f;
		if (toggleSize.y <= 0)
			toggleSize.y = ImGui::GetFrameHeight() * 0.8f;

		auto& style = ImGui::GetStyle();
		auto& colors = style.Colors;

		const ImVec4 toggleBg = *a_enabled ? colors[ImGuiCol_Header] : colors[ImGuiCol_FrameBg];
		const ImVec4 toggleBgHovered = *a_enabled ? colors[ImGuiCol_HeaderHovered] : colors[ImGuiCol_FrameBgHovered];
		const ImVec4 toggleBgActive = *a_enabled ? colors[ImGuiCol_HeaderActive] : colors[ImGuiCol_FrameBgActive];

		ImGui::PushStyleColor(ImGuiCol_Button, toggleBg);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toggleBgHovered);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, toggleBgActive);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, toggleSize.y * 0.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);

		ImGui::PushID(a_label);
		const bool clicked = ImGui::Button("", toggleSize);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 buttonMin = ImGui::GetItemRectMin();

		const float knobRadius = (toggleSize.y - 4.0f) * 0.5f;
		constexpr float knobPadding = 2.0f;
		const float knobTravel = toggleSize.x - (knobRadius * 2.0f) - (knobPadding * 2.0f);
		const float knobX = *a_enabled ?
		                        buttonMin.x + knobPadding + knobRadius + knobTravel :
		                        buttonMin.x + knobPadding + knobRadius;
		const float knobY = buttonMin.y + toggleSize.y * 0.5f;

		drawList->AddCircleFilled(ImVec2(knobX, knobY), knobRadius, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));

		ImGui::PopID();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);

		if (clicked)
			*a_enabled = !*a_enabled;

		return clicked;
	}

	bool DrawRoundedButtonHighlight(const ImVec2& a_min, const ImVec2& a_max, bool a_hovered, bool a_active, ImDrawList* a_drawList)
	{
		return DrawRoundedButtonHighlight(a_min, a_max, a_hovered, a_active, GetThemedButtonHighlightRounding(a_min, a_max), a_drawList);
	}

	bool DrawRoundedButtonHighlight(const ImVec2& a_min, const ImVec2& a_max, bool a_hovered, bool a_active, float a_rounding, ImDrawList* a_drawList)
	{
		if (!a_hovered && !a_active)
			return false;

		if (!a_drawList)
			a_drawList = ImGui::GetWindowDrawList();

		a_drawList->AddRectFilled(a_min, a_max,
			ImGui::GetColorU32(a_active ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), a_rounding);
		return true;
	}

	bool BeginWithRoundedClose(const char* a_name, bool* a_open, ImGuiWindowFlags a_flags)
	{
		bool visible = false;
		{
			NativeTitleBarButtonHighlightGuard guard;
			visible = ImGui::Begin(a_name, a_open, a_flags);
		}
		DrawRoundedTitleBarButtonHighlights(ImGui::GetCurrentWindowRead(), a_open != nullptr, true);
		return visible;
	}

	bool BeginPopupModalWithRoundedClose(const char* a_name, bool* a_open, ImGuiWindowFlags a_flags)
	{
		bool visible = false;
		{
			NativeTitleBarButtonHighlightGuard guard;
			visible = ImGui::BeginPopupModal(a_name, a_open, a_flags);
		}
		if (visible)
			DrawRoundedTitleBarButtonHighlights(ImGui::GetCurrentWindowRead(), a_open != nullptr, false);
		return visible;
	}

	ImVec2 GetNativeViewportSizeScaled(float a_scale)
	{
		const auto size = ImGui::GetMainViewport()->Size;
		return { size.x * a_scale, size.y * a_scale };
	}

	ImVec2 DrawSharpText(const char* a_text, bool a_alignToPixelGrid, float a_scale)
	{
		const ImVec2 startPos = ImGui::GetCursorPos();

		if (a_alignToPixelGrid) {
			ImVec2 pos = startPos;
			pos.x = std::round(pos.x);
			pos.y = std::round(pos.y);
			ImGui::SetCursorPos(pos);
		}
		if (a_scale != 1.0f)
			ImGui::SetWindowFontScale(a_scale);

		ImGui::Text("%s", a_text);

		if (a_scale != 1.0f)
			ImGui::SetWindowFontScale(1.0f);

		const ImVec2 endPos = ImGui::GetCursorPos();
		return ImVec2(endPos.x - startPos.x, endPos.y - startPos.y);
	}

	float GetCenterOffsetForContent(float a_contentWidth)
	{
		const float fullWindowWidth = ImGui::GetWindowWidth();
		const float windowPaddingX = ImGui::GetStyle().WindowPadding.x;
		const float availableFullWidth = fullWindowWidth - (windowPaddingX * 2.0f);
		const float centerOffset = (availableFullWidth - a_contentWidth) * 0.5f;
		const float offset = (windowPaddingX + centerOffset) - ImGui::GetCursorPosX();
		return offset > 0.0f ? offset : 0.0f;
	}

	bool DrawCategoryHeader(const char* a_categoryKey, const char* a_displayName, bool& a_isExpanded, int a_categoryCount)
	{
		ID3D11ShaderResourceView* categoryIcon = nullptr;
		auto& icons = Menu::Get().uiIcons;

		if (std::strcmp(a_categoryKey, "Lighting") == 0)
			categoryIcon = icons.lighting.texture;
		else if (std::strcmp(a_categoryKey, "Post-process") == 0)
			categoryIcon = icons.postProcessing.texture;
		else if (std::strcmp(a_categoryKey, "Compatibility") == 0)
			categoryIcon = icons.compatibility.texture;
		else if (std::strcmp(a_categoryKey, "Performance") == 0)
			categoryIcon = icons.performance.texture;
		else if (std::strcmp(a_categoryKey, "Dev Tools") == 0)
			categoryIcon = icons.devTools.texture;
		else
			categoryIcon = icons.misc.texture;

		const std::string headerText = std::format("{} ({})", a_displayName, a_categoryCount);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const float availableWidth = ImGui::GetContentRegionAvail().x;

		const float currentFontSize = ImGui::GetFontSize();
		const float iconSize = currentFontSize * 1.2f;
		const float iconSpacing = currentFontSize * 0.3f;
		const ImVec2 textSize = ImGui::CalcTextSize(headerText.c_str());

		float contentWidth = textSize.x;
		if (categoryIcon)
			contentWidth += iconSize + iconSpacing;

		const float lineY = pos.y + textSize.y * 0.5f;
		const float lineLength = (availableWidth - contentWidth - 20.0f) * 0.5f;

		ImGui::PushID(a_categoryKey);
		ImGui::SetCursorScreenPos(pos);
		const bool clicked = ImGui::InvisibleButton("##CategoryHeader", ImVec2(availableWidth, textSize.y + 4.0f));
		const bool hovered = ImGui::IsItemHovered();

		ImVec4 color = Menu::Get().GetTheme().FullPalette[ImGuiCol_Text];
		if (!a_isExpanded)
			color.w *= 0.7f;
		if (hovered)
			color.w *= 0.8f;

		const ImU32 headerColor = ImGui::GetColorU32(color);
		if (lineLength > 0)
			drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), headerColor, 1.0f);

		const float rightLineStart = pos.x + lineLength + 10.0f + contentWidth + 10.0f;
		if (rightLineStart < pos.x + availableWidth)
			drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), headerColor, 1.0f);

		float currentX = pos.x + lineLength + 10.0f;
		if (categoryIcon) {
			const ImVec2 iconPos(currentX, pos.y + (textSize.y - iconSize) * 0.5f + 2.0f);
			const ImVec2 iconMax(iconPos.x + iconSize, iconPos.y + iconSize);
			drawList->AddImage(categoryIcon, iconPos, iconMax, ImVec2(0, 0), ImVec2(1, 1), headerColor);
			currentX += iconSize + iconSpacing;
		}

		drawList->AddText(ImVec2(currentX, pos.y + 2.0f), headerColor, headerText.c_str());

		if (clicked)
			a_isExpanded = !a_isExpanded;

		ImGui::PopID();

		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));
		ImGui::Dummy(ImVec2(availableWidth, 0.0f));
		return clicked;
	}

	bool DrawSectionHeader(const char* a_sectionName, bool a_useWhiteText, bool a_isCollapsible, bool* a_isExpanded)
	{
		bool stateChanged = false;

		const auto& theme = Menu::Get().GetTheme().FeatureHeading;
		const auto& palette = Menu::Get().GetTheme().FullPalette;
		const ImVec4 color = a_useWhiteText ? palette[ImGuiCol_Text] : theme.ColorDefault;
		const ImU32 headerColor = ImGui::GetColorU32(color);

		if (a_isCollapsible && a_isExpanded) {
			ImGui::PushID(a_sectionName);
			ImGui::PushStyleColor(ImGuiCol_Text, headerColor);

			const bool open = ImGui::CollapsingHeader(a_sectionName, ImGuiTreeNodeFlags_DefaultOpen);
			stateChanged = open != *a_isExpanded;
			*a_isExpanded = open;

			ImGui::PopStyleColor();
			ImGui::PopID();
		} else {
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float availableWidth = ImGui::GetContentRegionAvail().x;
			const ImVec2 textSize = ImGui::CalcTextSize(a_sectionName);

			const float lineY = pos.y + textSize.y * 0.5f;
			const float lineLength = (availableWidth - textSize.x - 20.0f) * 0.5f;

			if (lineLength > 0)
				drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), headerColor, 1.0f);

			const float rightLineStart = pos.x + lineLength + 10.0f + textSize.x + 10.0f;
			if (rightLineStart < pos.x + availableWidth)
				drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), headerColor, 1.0f);

			drawList->AddText(ImVec2(pos.x + lineLength + 10.0f, pos.y + 2.0f), headerColor, a_sectionName);

			ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));
			ImGui::Dummy(ImVec2(availableWidth, 0.0f));
		}

		return stateChanged;
	}

	void DrawSearchIcon(const ImVec2& a_position, float a_size, float a_alpha)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const ImVec2 center(a_position.x + a_size * 0.46f, a_position.y + a_size * 0.5f);
		const float radius = a_size * 0.3f;
		const float circleStroke = a_size * ThemeManager::Constants::SEARCH_ICON_STROKE_RATIO;
		const float handleStroke = a_size * ThemeManager::Constants::SEARCH_ICON_HANDLE_STROKE_RATIO;

		ImVec4 iconColor = Menu::Get().GetTheme().FullPalette[ImGuiCol_Text];
		iconColor.w *= a_alpha;
		const ImU32 placeholderColor = ImGui::GetColorU32(iconColor);

		drawList->AddCircle(center, radius, placeholderColor, 12, circleStroke);

		const ImVec2 handleStart(center.x + radius * 0.81f, center.y + radius * 0.81f);
		const ImVec2 handleEnd(handleStart.x + a_size * 0.29f, handleStart.y + a_size * 0.29f);
		drawList->AddLine(handleStart, handleEnd, placeholderColor, handleStroke);
	}

	std::string DrawComboSearchInput(const char* a_id)
	{
		auto& state = GetComboSearchStates()[a_id];

		if (state.needsFocus) {
			ImGui::SetKeyboardFocusHere();
			state.needsFocus = false;
		}

		const float scale = GetSearchUIScale();
		const float iconSize = ThemeManager::Constants::COMBO_SEARCH_ICON_SIZE * scale;
		const float iconOffsetX = ThemeManager::Constants::COMBO_SEARCH_ICON_OFFSET_X * scale;
		const float paddingLeft = ThemeManager::Constants::COMBO_SEARCH_PADDING_LEFT * scale;

		char widgetId[128];
		std::snprintf(widgetId, sizeof(widgetId), "##%s_search", a_id);

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(paddingLeft, ImGui::GetStyle().FramePadding.y));
		ImGui::InputTextWithHint(widgetId, "Search...", state.buffer, IM_ARRAYSIZE(state.buffer));
		ImGui::PopStyleVar();

		const ImVec2 iconPos(
			ImGui::GetItemRectMin().x + iconOffsetX,
			ImGui::GetItemRectMin().y + (ImGui::GetItemRectSize().y - iconSize) * 0.5f);
		DrawSearchIcon(iconPos, iconSize, ThemeManager::Constants::COMBO_SEARCH_ICON_ALPHA);

		ImGui::Separator();

		return state.buffer;
	}

	void ClearComboSearch(const char* a_id)
	{
		auto& state = GetComboSearchStates()[a_id];
		state.buffer[0] = '\0';
		state.needsFocus = true;
	}

	void DrawFeatureSearchBar(std::string& a_searchString, float a_availableWidth)
	{
		ImGui::PushID("FeatureSearchBar");

		const float scale = GetSearchUIScale();
		const float iconSize = ThemeManager::Constants::SEARCH_ICON_SIZE * scale;
		const float iconSpace = iconSize + ThemeManager::Constants::SEARCH_INPUT_PADDING_EXTRA * scale;

		const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		if (a_availableWidth <= 0.0f)
			a_availableWidth = ImGui::GetContentRegionAvail().x;
		const float frameHeight = ImGui::GetFrameHeight();

		const ImVec4 bgColor(0.0f, 0.0f, 0.0f, 0.0f);
		const ImVec4 bgColorActive(0.3f, 0.3f, 0.3f, 0.9f);
		const ImVec4 textColor = Menu::Get().GetTheme().FullPalette[ImGuiCol_Text];

		ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, bgColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, bgColorActive);
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(iconSpace, ThemeManager::Constants::SEARCH_INPUT_FRAME_PADDING_Y * scale));

		ImGui::SetNextItemWidth(a_availableWidth);
		char buffer[256];
		strncpy_s(buffer, a_searchString.c_str(), sizeof(buffer) - 1);
		buffer[sizeof(buffer) - 1] = '\0';

		if (ImGui::InputTextWithHint("##feature_search", "Search Features...", buffer, sizeof(buffer)))
			a_searchString = buffer;

		const ImVec2 iconPos(
			cursorPos.x + ThemeManager::Constants::SEARCH_ICON_OFFSET_X * scale,
			cursorPos.y + (frameHeight - iconSize) * 0.5f);
		DrawSearchIcon(iconPos, iconSize, ThemeManager::Constants::SEARCH_ICON_ALPHA);

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(5);
		ImGui::PopID();
	}

	bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
	{
		return a_lhs.size() == a_rhs.size() &&
		       std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), [](unsigned char a_l, unsigned char a_r) {
			       return std::tolower(a_l) == std::tolower(a_r);
		       });
	}

	bool StringMatchesSearch(std::string_view a_text, std::string_view a_searchQuery)
	{
		if (a_searchQuery.empty())
			return true;
		return ToLower(a_text).find(ToLower(a_searchQuery)) != std::string::npos;
	}

	bool FeatureMatchesSearch(const Feature* a_feature, const std::string& a_searchQuery)
	{
		if (a_searchQuery.empty())
			return true;
		if (!a_feature)
			return false;

		if (StringMatchesSearch(a_feature->GetDisplayName(), a_searchQuery))
			return true;
		if (StringMatchesSearch(a_feature->GetName(), a_searchQuery))
			return true;
		if (StringMatchesSearch(a_feature->GetCategory(), a_searchQuery))
			return true;
		return StringMatchesSearch(a_feature->GetConfigKey(), a_searchQuery);
	}

	std::string GetFormattedVersion()
	{
		return std::format("{}.{}.{}", Plugin::VERSION.major(), Plugin::VERSION.minor(), Plugin::VERSION.patch());
	}

	std::string GetMenuDisplayTitle()
	{
		const auto version = GetFormattedVersion();
		const auto expectedTag = std::format("v{}", version);
		return std::string_view(CS_BUILD_DESCRIBE) == expectedTag ?
		           std::format("Fallout 4 Community Shaders {}", version) :
		           std::format("Fallout 4 Community Shaders {} [{}]", version, CS_BUILD_DESCRIBE);
	}

	bool LoadTextureFromFile(ID3D11Device* a_device, const char* a_filename,
		ID3D11ShaderResourceView** a_outSrv, ImVec2& a_outSize)
	{
		if (!a_device || !a_filename || !a_outSrv)
			return false;

		const std::filesystem::path path(a_filename);
		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return false;

		const std::wstring widePath = path.wstring();
		Microsoft::WRL::ComPtr<ID3D11Resource> resource;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		if (FAILED(DirectX::CreateWICTextureFromFile(a_device, widePath.c_str(), resource.GetAddressOf(), srv.GetAddressOf())))
			return false;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		if (FAILED(resource.As(&texture)))
			return false;

		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		a_outSize = ImVec2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));

		*a_outSrv = srv.Detach();
		return true;
	}

	namespace Colors
	{
		ImVec4 GetSuccess() { return Menu::Get().GetTheme().StatusPalette.SuccessColor; }
		ImVec4 GetWarning() { return Menu::Get().GetTheme().StatusPalette.Warning; }
		ImVec4 GetError() { return Menu::Get().GetTheme().StatusPalette.Error; }
		ImVec4 GetInfo() { return Menu::Get().GetTheme().StatusPalette.InfoColor; }
	}

	namespace Text
	{
		namespace
		{
			void ColoredTextV(ImVec4 a_color, const char* a_fmt, va_list a_args)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, a_color);
				ImGui::TextV(a_fmt, a_args);
				ImGui::PopStyleColor();
			}

			void ColoredTextWrappedV(ImVec4 a_color, const char* a_fmt, va_list a_args)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, a_color);
				ImGui::TextWrappedV(a_fmt, a_args);
				ImGui::PopStyleColor();
			}
		}

#define CS_UI_TEXT(Name, ColorFn)                   \
	void Name(const char* a_fmt, ...)               \
	{                                               \
		va_list args;                               \
		va_start(args, a_fmt);                      \
		ColoredTextV(Colors::ColorFn(), a_fmt, args); \
		va_end(args);                               \
	}
#define CS_UI_TEXT_WRAPPED(Name, ColorFn)                  \
	void Name(const char* a_fmt, ...)                      \
	{                                                      \
		va_list args;                                      \
		va_start(args, a_fmt);                             \
		ColoredTextWrappedV(Colors::ColorFn(), a_fmt, args); \
		va_end(args);                                      \
	}

		CS_UI_TEXT(Warning, GetWarning)
		CS_UI_TEXT_WRAPPED(WrappedWarning, GetWarning)
		CS_UI_TEXT(Error, GetError)
		CS_UI_TEXT_WRAPPED(WrappedError, GetError)
		CS_UI_TEXT(Success, GetSuccess)
		CS_UI_TEXT_WRAPPED(WrappedSuccess, GetSuccess)
		CS_UI_TEXT(Info, GetInfo)
		CS_UI_TEXT_WRAPPED(WrappedInfo, GetInfo)

#undef CS_UI_TEXT
#undef CS_UI_TEXT_WRAPPED
	}

	namespace Input
	{
		const char* KeyIdToString(std::uint32_t a_key)
		{
			if (a_key >= 256)
				return "";

			static const char* keyboardKeysInternational[256] = {
				"", "Left Mouse", "Right Mouse", "Cancel", "Middle Mouse", "X1 Mouse", "X2 Mouse", "", "Backspace", "Tab", "", "", "Clear", "Enter", "", "",
				"Shift", "Control", "Alt", "Pause", "Caps Lock", "", "", "", "", "", "", "Escape", "", "", "", "",
				"Space", "Page Up", "Page Down", "End", "Home", "Left Arrow", "Up Arrow", "Right Arrow", "Down Arrow", "Select", "", "", "Print Screen", "Insert", "Delete", "Help",
				"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", "", "", "", "",
				"", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
				"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Left Windows", "Right Windows", "Apps", "", "Sleep",
				"Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Numpad *", "Numpad +", "", "Numpad -", "Numpad Decimal", "Numpad /",
				"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16",
				"F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "", "", "", "", "", "", "", "",
				"Num Lock", "Scroll Lock", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
				"Left Shift", "Right Shift", "Left Control", "Right Control", "Left Menu", "Right Menu", "Browser Back", "Browser Forward", "Browser Refresh", "Browser Stop", "Browser Search", "Browser Favorites", "Browser Home", "Volume Mute", "Volume Down", "Volume Up",
				"Next Track", "Previous Track", "Media Stop", "Media Play/Pause", "Mail", "Media Select", "Launch App 1", "Launch App 2", "", "", "OEM ;", "OEM +", "OEM ,", "OEM -", "OEM .", "OEM /",
				"OEM ~", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
				"", "", "", "", "", "", "", "", "", "", "", "OEM [", "OEM \\", "OEM ]", "OEM '", "OEM 8",
				"", "", "OEM <", "", "", "", "", "", "", "", "", "", "", "", "", "",
				"", "", "", "", "", "", "Attn", "CrSel", "ExSel", "Erase EOF", "Play", "Zoom", "", "PA1", "OEM Clear", ""
			};

			return keyboardKeysInternational[a_key];
		}

		std::string KeyIdToString(const std::vector<InputCombo>& a_combo)
		{
			if (a_combo.empty())
				return "None";

			std::string result;
			for (std::size_t i = 0; i < a_combo.size(); ++i) {
				if (i > 0)
					result += " + ";
				result += KeyIdToString(a_combo[i].GetKey());
			}
			return result;
		}
	}

	bool InputComboWidget(
		const char* a_label,
		std::vector<InputCombo>& a_combo,
		std::atomic<bool>& a_isRecording,
		const char* a_recordingLabel,
		bool a_allowClear)
	{
		bool changed = false;
		ImGui::Text("%s", a_label);
		ImGui::SameLine();

		const auto& theme = Menu::Get().GetTheme().StatusPalette;
		const bool isRecording = a_isRecording.load(std::memory_order_acquire);

		if (isRecording) {
			ImGui::PushStyleColor(ImGuiCol_Button, theme.CurrentHotkey);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.CurrentHotkey);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.CurrentHotkey);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));

			std::string buttonText;
			if (!a_combo.empty())
				buttonText = Input::KeyIdToString(a_combo) + "...";
			else
				buttonText = "Recording... (Esc to cancel)";

			if (ImGui::Button(buttonText.c_str(), ImVec2(0, 0)))
				a_isRecording.store(false, std::memory_order_release);

			ImGui::PopStyleColor(4);

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Press any key combination.\nModifiers (Ctrl, Shift, Alt) are supported.\nPress Escape to cancel.");
		} else {
			const std::string btnLabel = Input::KeyIdToString(a_combo) + "##" + a_recordingLabel;
			if (ImGui::Button(btnLabel.c_str(), ImVec2(0, 0)))
				a_isRecording.store(true, std::memory_order_release);

			if (a_allowClear && ImGui::BeginPopupContextItem()) {
				if (ImGui::Selectable("Clear Binding")) {
					a_combo.clear();
					changed = true;
				}
				ImGui::EndPopup();
			}

			if (a_combo.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("(Click to bind)");
			}
		}

		return changed;
	}
}
