#pragma once

#include "Menu/Menu.h"

#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

namespace cs::MenuFonts
{
	using FontRole = Menu::FontRole;
	using FontRoleSettings = Menu::ThemeSettings::FontRoleSettings;

	void NormalizeFontRoles(Menu::ThemeSettings& a_theme, bool a_themeProvidedFontRoles);
	const FontRoleSettings& GetDefaultRole(FontRole a_role);
	std::string BuildFontSignature(const Menu::ThemeSettings& a_theme, float a_baseFontSize);

	class ImFontGuard
	{
	public:
		explicit ImFontGuard(ImFont* a_font);
		~ImFontGuard();

		ImFontGuard(const ImFontGuard&) = delete;
		ImFontGuard& operator=(const ImFontGuard&) = delete;

	private:
		bool pushed_ = false;
	};

	class FontRoleGuard
	{
	public:
		explicit FontRoleGuard(FontRole a_role);
		~FontRoleGuard();

		FontRoleGuard(const FontRoleGuard&) = delete;
		FontRoleGuard& operator=(const FontRoleGuard&) = delete;

		[[nodiscard]] ImFont* Get() const { return font_; }

	private:
		ImFont* font_ = nullptr;
		std::optional<ImFontGuard> guard_;
	};

	// Scales frame padding when the tab font is larger than body text so tab bars keep their height.
	class TabBarPaddingGuard
	{
	public:
		explicit TabBarPaddingGuard(FontRole a_tabFontRole);
		~TabBarPaddingGuard();

		TabBarPaddingGuard(const TabBarPaddingGuard&) = delete;
		TabBarPaddingGuard& operator=(const TabBarPaddingGuard&) = delete;

	private:
		ImVec2 originalPadding_;
		bool scaled_ = false;
	};

	bool BeginTabItemWithFont(const char* a_label, FontRole a_role, ImGuiTabItemFlags a_flags = ImGuiTabItemFlags_None);

	// Must run after role fonts are added and before io.Fonts->Build().
	void AddPreviewFontsToAtlas(float a_previewFontSize);
	void InvalidatePreviewFonts();
	[[nodiscard]] ImFont* GetPreviewFont(const std::string& a_file);
}

namespace cs::fonts
{
	struct StyleInfo
	{
		std::string style;
		std::string displayName;
		std::string file;
		std::string family;
	};

	struct FamilyInfo
	{
		std::string name;
		std::string displayName;
		std::vector<StyleInfo> styles;
	};

	struct Catalog
	{
		std::vector<FamilyInfo> families;

		const FamilyInfo* FindFamily(const std::string& a_name) const;
		const StyleInfo* FindStyle(const std::string& a_family, const std::string& a_style) const;
	};

	Catalog DiscoverFontCatalog(bool a_forceRefresh = false);
	std::string FormatFontDisplayName(const std::string& a_filename);

	[[nodiscard]] const StyleInfo* FindRegularStyle(const FamilyInfo& a_family);
	[[nodiscard]] int FindFamilyIndex(const Catalog& a_catalog, const std::string& a_familyName);
	[[nodiscard]] int FindStyleIndex(const FamilyInfo& a_family, const std::string& a_styleName);

	std::vector<std::string> DiscoverFonts();
	bool ValidateFont(const std::string& a_fontName);
}
