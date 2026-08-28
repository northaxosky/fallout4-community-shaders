#pragma once

#include "Menu/ThemeManager.h"
#include "Utils/Input.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <windows.h>

struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct ImRect;

namespace cs
{
	class Feature;
	class Menu;
}

namespace cs::ui
{
	constexpr float DefaultHeaderTextScale = 1.5f;

	// Layout constants target the 1080p font size.
	constexpr float kBaselineFontSize = ThemeManager::Constants::DEFAULT_SCREEN_HEIGHT * ThemeManager::Constants::DEFAULT_FONT_RATIO;

	inline float GetUIScaleForBaseline(float a_baselineFontSize) { return ImGui::GetFontSize() / a_baselineFontSize; }
	inline float GetUIScale() { return GetUIScaleForBaseline(kBaselineFontSize); }
	inline float GetSearchUIScale() { return GetUIScaleForBaseline(ThemeManager::Constants::SEARCH_BASELINE_SCREEN_HEIGHT * ThemeManager::Constants::DEFAULT_FONT_RATIO); }

	namespace paths
	{
		const std::filesystem::path& GetPluginPath();
		std::filesystem::path GetFontsPath();
		std::filesystem::path GetThemesPath();
		std::filesystem::path GetIconsPath();
		std::filesystem::path GetImGuiIniPath();
		bool IsPathWithinDirectory(const std::filesystem::path& a_basePath, const std::filesystem::path& a_testPath);
	}

	class HoverTooltipWrapper
	{
	public:
		HoverTooltipWrapper();
		~HoverTooltipWrapper();
		HoverTooltipWrapper(const HoverTooltipWrapper&) = delete;
		HoverTooltipWrapper& operator=(const HoverTooltipWrapper&) = delete;
		operator bool() const { return hovered; }

	private:
		bool hovered;
	};

	class CenteredPopupModal
	{
	public:
		static constexpr ImVec2 kPopupCenter{ -FLT_MAX, -FLT_MAX };

		explicit CenteredPopupModal(const char* a_name,
			bool* a_open = nullptr,
			ImGuiWindowFlags a_flags = ImGuiWindowFlags_AlwaysAutoResize,
			ImVec2 a_pos = kPopupCenter,
			ImVec2 a_pivot = ImVec2(0.5f, 0.5f));
		~CenteredPopupModal();
		operator bool() const { return isOpen; }

		CenteredPopupModal(const CenteredPopupModal&) = delete;
		CenteredPopupModal& operator=(const CenteredPopupModal&) = delete;

	private:
		bool isOpen;
	};

	class DisableGuard
	{
	public:
		explicit DisableGuard(bool a_disable);
		~DisableGuard();
		DisableGuard(const DisableGuard&) = delete;
		DisableGuard& operator=(const DisableGuard&) = delete;

	private:
		bool disable;
	};

	class StyledButtonWrapper
	{
	public:
		StyledButtonWrapper(const ImVec4& a_normal, const ImVec4& a_hovered, const ImVec4& a_active);
		~StyledButtonWrapper();
		StyledButtonWrapper(StyledButtonWrapper&& a_other) noexcept;
		StyledButtonWrapper(const StyledButtonWrapper&) = delete;
		StyledButtonWrapper& operator=(const StyledButtonWrapper&) = delete;

	private:
		int m_pushedStyles;
	};

	struct ConfirmationPopup
	{
		std::string title;
		std::string message;
		std::string confirmLabel = "Confirm";
		std::string cancelLabel = "Cancel";
		bool showDontAskAgain = false;
		bool* dontAskAgainPersist = nullptr;

		ConfirmationPopup() = default;
		ConfirmationPopup(std::string a_title, std::string a_message,
			std::string a_confirmLabel = "Confirm", std::string a_cancelLabel = "Cancel") :
			title(std::move(a_title)),
			message(std::move(a_message)),
			confirmLabel(std::move(a_confirmLabel)),
			cancelLabel(std::move(a_cancelLabel)) {}

		void Request();
		bool Draw();
		bool IsOpen() const { return show; }

	private:
		bool show = false;
		bool dontAskCheckbox = false;
	};

	StyledButtonWrapper StatusButtonStyle(const ImVec4& a_color);
	StyledButtonWrapper DestructiveButtonStyle();
	ImVec4 GetIconTint();

	bool ButtonWithFlash(const char* a_label, const ImVec2& a_size = ImVec2(0, 0), int a_flashDurationMs = 200);
	bool ErrorButton(const char* a_label, const ImVec2& a_size = ImVec2(0, 0));
	bool FeatureToggle(const char* a_label, bool* a_enabled, const ImVec2& a_size = ImVec2(0, 0));

	bool DrawRoundedButtonHighlight(const ImVec2& a_min, const ImVec2& a_max, bool a_hovered, bool a_active, ImDrawList* a_drawList = nullptr);
	bool DrawRoundedButtonHighlight(const ImVec2& a_min, const ImVec2& a_max, bool a_hovered, bool a_active, float a_rounding, ImDrawList* a_drawList);

	bool BeginWithRoundedClose(const char* a_name, bool* a_open, ImGuiWindowFlags a_flags = 0);
	bool BeginPopupModalWithRoundedClose(const char* a_name, bool* a_open = nullptr, ImGuiWindowFlags a_flags = 0);

	ImVec2 GetNativeViewportSizeScaled(float a_scale);

	ImVec2 DrawSharpText(const char* a_text, bool a_alignToPixelGrid = true, float a_scale = 1.0f);
	float GetCenterOffsetForContent(float a_contentWidth);

	bool DrawCategoryHeader(const char* a_categoryKey, const char* a_displayName, bool& a_isExpanded, int a_categoryCount);
	bool DrawSectionHeader(const char* a_sectionName, bool a_useWhiteText = false, bool a_isCollapsible = true, bool* a_isExpanded = nullptr);

	void DrawSearchIcon(const ImVec2& a_position, float a_size = ThemeManager::Constants::SEARCH_ICON_SIZE, float a_alpha = ThemeManager::Constants::SEARCH_ICON_ALPHA);
	void DrawFeatureSearchBar(std::string& a_searchString, float a_availableWidth = 0.0f);
	std::string DrawComboSearchInput(const char* a_id);
	void ClearComboSearch(const char* a_id);

	bool IEquals(std::string_view a_lhs, std::string_view a_rhs);
	bool StringMatchesSearch(std::string_view a_text, std::string_view a_searchQuery);
	bool FeatureMatchesSearch(const Feature* a_feature, const std::string& a_searchQuery);
	std::string GetFormattedVersion();
	std::string GetMenuDisplayTitle();

	bool LoadTextureFromFile(ID3D11Device* a_device, const char* a_filename,
		ID3D11ShaderResourceView** a_outSrv, ImVec2& a_outSize);

	namespace Colors
	{
		ImVec4 GetSuccess();
		ImVec4 GetWarning();
		ImVec4 GetError();
		ImVec4 GetInfo();
	}

	namespace Text
	{
		void Warning(const char* a_fmt, ...);
		void WrappedWarning(const char* a_fmt, ...);
		void Error(const char* a_fmt, ...);
		void WrappedError(const char* a_fmt, ...);
		void Success(const char* a_fmt, ...);
		void WrappedSuccess(const char* a_fmt, ...);
		void Info(const char* a_fmt, ...);
		void WrappedInfo(const char* a_fmt, ...);
	}

	namespace Input
	{
		const char* KeyIdToString(std::uint32_t a_key);
		std::string KeyIdToString(const std::vector<InputCombo>& a_combo);
	}

	bool InputComboWidget(
		const char* a_label,
		std::vector<InputCombo>& a_combo,
		std::atomic<bool>& a_isRecording,
		const char* a_recordingLabel,
		bool a_allowClear = true);
}
