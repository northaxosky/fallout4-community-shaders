#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>

namespace cs
{
	class Menu;

	// Owns the ImGui style/palette application and the on-disk theme preset catalogue.
	class ThemeManager
	{
	public:
		struct ThemeInfo
		{
			std::string name;
			std::string displayName;
			std::string description;
			std::string filePath;
			toml::table themeData;
			bool isValid = false;

			std::string version;
			std::string author;
		};

		static float ResolveFontSize(const Menu& a_menu);

		static void SetupImGuiStyle(const Menu& a_menu);
		static void InitDefaultFontConfig(ImFontConfig& a_config);
		static bool ReloadFont(const Menu& a_menu, float& a_cachedFontSize);
		static void ForceApplyDefaultTheme();

		struct Constants
		{
			static constexpr float DEFAULT_SCREEN_HEIGHT = 1080.0f;
			static constexpr float DEFAULT_FONT_RATIO = (7.0f / 360.0f);  // 21px @ 1080p, 28px @ 1440p, 42px @ 4K
			static constexpr float MIN_FONT_SIZE = 16.0f;
			static constexpr float MAX_FONT_SIZE = 108.0f;
			static constexpr float DEFAULT_FONT_SIZE = 27.0f;

			static constexpr float DEFAULT_GLOBAL_SCALE = 0.0f;

			static constexpr int FCONF_OVERSAMPLE_H = 3;
			static constexpr int FCONF_OVERSAMPLE_V = 2;
			static constexpr bool FCONF_PIXELSNAP_H = true;
			static constexpr float FCONF_RASTERIZER_MULTIPLY = 1.1f;

			static constexpr float HEADER_BASE_TEXT_SCALE = 1.7f;
			static constexpr float HEADER_BASE_ICON_MULTIPLIER = 1.85f;
			static constexpr float HEADER_FALLBACK_TEXT_SCALE = 1.5f;
			static constexpr float DOCKED_ICON_SIZE_MULTIPLIER = 1.5f;
			static constexpr float DOCKED_ICON_SPACING = 8.0f;
			static constexpr float DOCKED_RIGHT_MARGIN = 45.0f;
			static constexpr float UNDOCKED_ICON_PADDING_REDUCTION = 4.0f;
			static constexpr float DOCKED_ICON_PADDING_REDUCTION = 2.0f;

			static constexpr float BUTTON_PADDING = 16.0f;
			static constexpr float BUTTON_SPACING = 8.0f;
			static constexpr float OVERLAY_WINDOW_POSITION = 10.0f;
			static constexpr float FONT_CACHE_EPSILON = 0.01f;
			static constexpr float CURSOR_POSITION_PADDING = 14.0f;
			static constexpr float SEPARATOR_THICKNESS = 3.0f;
			static constexpr float UNDOCKED_ICON_ITEM_SPACING = 6.0f;
			static constexpr float POPUP_BUTTON_WIDTH = 180.0f;

			static constexpr float DEFAULT_FEATURE_TITLE_SCALE = 1.5f;
			static constexpr float VERSION_TEXT_OPACITY = 0.6f;

			static constexpr float AUTOHIDE_ACTIVATION_ZONE_WIDTH = 50.0f;
			static constexpr float AUTOHIDE_EXPAND_DELAY = 0.25f;
			static constexpr float AUTOHIDE_PANEL_WIDTH_RATIO = 0.2f;

			static constexpr float SEARCH_BASELINE_SCREEN_HEIGHT = 1440.0f;
			static constexpr float SEARCH_ICON_SIZE = 20.0f;
			static constexpr float SEARCH_ICON_ALPHA = 0.7f;
			static constexpr float SEARCH_ICON_OFFSET_X = 8.0f;
			static constexpr float SEARCH_INPUT_PADDING_EXTRA = 14.0f;
			static constexpr float SEARCH_INPUT_FRAME_PADDING_Y = 6.0f;
			static constexpr float SEARCH_ICON_STROKE_RATIO = 0.11f;
			static constexpr float SEARCH_ICON_HANDLE_STROKE_RATIO = 0.105f;
			static constexpr float COMBO_SEARCH_ICON_SIZE = 16.0f;
			static constexpr float COMBO_SEARCH_ICON_ALPHA = 0.5f;
			static constexpr float COMBO_SEARCH_ICON_OFFSET_X = 5.0f;
			static constexpr float COMBO_SEARCH_PADDING_LEFT = 24.0f;

			static constexpr float BUTTON_MIN_COLOR_CHANNEL = 0.0f;
			static constexpr float BUTTON_MAX_COLOR_CHANNEL = 1.0f;
			static constexpr float BUTTON_HOVER_BRIGHTEN = 0.2f;
			static constexpr float BUTTON_ACTIVE_BRIGHTEN = 0.3f;
			static constexpr float BUTTON_STATUS_TEXT_HOVER_ALPHA = 0.8f;
			static constexpr float BUTTON_STATUS_TEXT_ACTIVE_ALPHA = 1.0f;
		};

		static ThemeManager& Get();

		std::size_t DiscoverThemes();
		std::vector<std::string> GetThemeNames() const;

		bool LoadTheme(const std::string& a_themeName, toml::table& a_themeSettings);
		bool SaveTheme(const std::string& a_themeName,
			const toml::table& a_themeSettings,
			const std::string& a_displayName,
			const std::string& a_description);

		const ThemeInfo* GetThemeInfo(const std::string& a_themeName) const;
		void RefreshThemes();
		bool IsDiscovered() const { return discovered; }
		bool IsPresetTheme(const std::string& a_themeName) const;
		std::filesystem::path GetThemesDirectory() const;
		void CreateDefaultThemeFiles();

	private:
		ThemeManager() = default;
		~ThemeManager() = default;
		ThemeManager(const ThemeManager&) = delete;
		ThemeManager& operator=(const ThemeManager&) = delete;

		std::unique_ptr<ThemeInfo> LoadThemeFile(const std::filesystem::path& a_filePath);
		bool ValidateThemeData(const toml::table& a_themeData) const;

		std::vector<ThemeInfo> themes;
		bool discovered = false;

		static constexpr std::size_t MAX_THEMES = 100;
		static constexpr std::size_t MAX_FILE_SIZE = 1024 * 1024;
	};
}
