#include "Menu/Fonts.h"

#include "Menu/ThemeManager.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
	constexpr std::size_t RoleIndex(cs::Menu::FontRole a_role)
	{
		return static_cast<std::size_t>(a_role);
	}

	const cs::Menu::ThemeSettings::FontRoleSettings& GetDefaultRoleInternal(cs::Menu::FontRole a_role)
	{
		static const cs::Menu::ThemeSettings defaults{};
		return defaults.FontRoles[RoleIndex(a_role)];
	}

	std::string NormalizeFontFilePath(const std::string& a_path)
	{
		if (a_path.empty())
			return {};

		std::filesystem::path asPath(a_path);
		auto normalized = asPath.generic_string();
		while (!normalized.empty() && (normalized.front() == '/' || normalized.front() == '\\'))
			normalized.erase(normalized.begin());
		return normalized;
	}

	void DeriveFamilyAndStyle(cs::MenuFonts::FontRoleSettings& a_role)
	{
		if (a_role.File.empty())
			return;

		const std::filesystem::path relative(a_role.File);
		if (a_role.Family.empty()) {
			const auto parent = relative.parent_path();
			if (!parent.empty()) {
				a_role.Family = parent.begin()->string();
			} else {
				const auto stem = relative.stem().string();
				const auto split = stem.find_first_of("-_ ");
				a_role.Family = split != std::string::npos ? stem.substr(0, split) : stem;
			}
		}
		if (a_role.Style.empty()) {
			const auto stem = relative.stem().string();
			const auto split = stem.find_first_of("-_ ");
			if (split != std::string::npos && split + 1 < stem.size())
				a_role.Style = stem.substr(split + 1);
			else
				a_role.Style = "Regular";
		}
	}

	void ApplyRoleDefaults(cs::MenuFonts::FontRoleSettings& a_target, cs::Menu::FontRole a_role)
	{
		const auto& defaults = GetDefaultRoleInternal(a_role);
		if (a_target.File.empty())
			a_target.File = defaults.File;
		if (a_target.SizeScale <= 0.0f)
			a_target.SizeScale = defaults.SizeScale;
		if (a_target.Family.empty())
			a_target.Family = defaults.Family;
		if (a_target.Style.empty())
			a_target.Style = defaults.Style;
	}

	std::string ToLowerCopy(std::string a_value)
	{
		std::transform(a_value.begin(), a_value.end(), a_value.begin(),
			[](unsigned char a_ch) { return static_cast<char>(std::tolower(a_ch)); });
		return a_value;
	}

	std::vector<std::string> SplitTokens(const std::string& a_text)
	{
		std::vector<std::string> tokens;
		std::string token;
		for (const char ch : a_text) {
			if (ch == '-' || ch == '_' || ch == ' ') {
				if (!token.empty()) {
					tokens.push_back(token);
					token.clear();
				}
			} else {
				token += ch;
			}
		}
		if (!token.empty())
			tokens.push_back(token);
		return tokens;
	}

	// Width variants belong to the style, not the family name.
	bool IsWidthVariant(const std::string& a_token)
	{
		static const std::vector<std::string> widthVariants = {
			"condensed", "narrow", "compressed", "compact",
			"extended", "expanded", "wide",
			"ultracompressed", "ultracondensed", "ultraexpanded"
		};
		const std::string lower = ToLowerCopy(a_token);
		return std::find(widthVariants.begin(), widthVariants.end(), lower) != widthVariants.end();
	}

	std::string ExtractFamilyName(const std::filesystem::path& a_relativePath)
	{
		if (a_relativePath.has_parent_path()) {
			auto it = a_relativePath.begin();
			if (it != a_relativePath.end())
				return it->string();
		}

		const auto stem = a_relativePath.stem().string();
		const auto tokens = SplitTokens(stem);

		std::string family;
		for (const auto& token : tokens) {
			if (IsWidthVariant(token))
				break;
			if (!family.empty())
				family += " ";
			family += token;
		}

		if (family.empty()) {
			const auto pos = stem.find_first_of("-_ ");
			family = (pos != std::string::npos && pos > 0) ? stem.substr(0, pos) : stem;
		}

		return family;
	}

	std::string ExtractStyleName(const std::filesystem::path& a_relativePath, const std::string& a_family)
	{
		std::string stem = a_relativePath.stem().string();
		const std::string lowerStem = ToLowerCopy(stem);
		const std::string lowerFamily = ToLowerCopy(a_family);

		if (!lowerFamily.empty()) {
			const std::string hyphen = lowerFamily + "-";
			const std::string underscore = lowerFamily + "_";
			const std::string space = lowerFamily + " ";
			if (lowerStem.rfind(hyphen, 0) == 0 || lowerStem.rfind(underscore, 0) == 0 || lowerStem.rfind(space, 0) == 0)
				stem = stem.substr(a_family.size() + 1);
		}

		std::string style;
		for (const auto& token : SplitTokens(stem)) {
			if (!style.empty())
				style += " ";
			style += token;
		}

		if (style.empty() || ToLowerCopy(style) == lowerFamily)
			style = "Regular";
		return style;
	}

	std::string ToDisplayLabel(const std::string& a_token)
	{
		if (a_token.empty())
			return "Regular";

		std::string display;
		display.reserve(a_token.size() + 4);
		char prev = '\0';
		for (const char ch : a_token) {
			if (ch == '-' || ch == '_') {
				if (!display.empty() && display.back() != ' ')
					display.push_back(' ');
				prev = ' ';
				continue;
			}
			if (!display.empty() && std::islower(static_cast<unsigned char>(prev)) && std::isupper(static_cast<unsigned char>(ch)))
				display.push_back(' ');
			display.push_back(ch);
			prev = ch;
		}
		if (!display.empty())
			display[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(display[0])));
		return display;
	}

	std::string NormalizeRelativeFontPath(const std::filesystem::path& a_root, const std::filesystem::path& a_absolute)
	{
		auto normalized = a_absolute.lexically_relative(a_root).generic_string();
		while (!normalized.empty() && (normalized.front() == '/' || normalized.front() == '\\'))
			normalized.erase(normalized.begin());
		return normalized;
	}

	std::unordered_map<std::string, ImFont*> g_previewFontsByFile;

	const ImWchar g_previewGlyphRanges[] = {
		0x0020, 0x00FF,  // Basic Latin plus Latin-1 Supplement
		0,
	};

	// Capped so a large font folder cannot overflow the atlas during Build().
	constexpr std::size_t MAX_PREVIEW_FONTS = 64;
	constexpr std::size_t MAX_PREVIEW_FONT_FAILURES = 3;
}

namespace cs::MenuFonts
{
	void NormalizeFontRoles(Menu::ThemeSettings& a_theme, bool a_themeProvidedFontRoles)
	{
		if (!a_themeProvidedFontRoles && !a_theme.FontName.empty())
			a_theme.FontRoles[RoleIndex(Menu::FontRole::Body)].File = NormalizeFontFilePath(a_theme.FontName);

		for (std::size_t i = 0; i < static_cast<std::size_t>(Menu::FontRole::Count); ++i) {
			const auto role = static_cast<Menu::FontRole>(i);
			auto& settings = a_theme.FontRoles[i];
			settings.File = NormalizeFontFilePath(settings.File);
			ApplyRoleDefaults(settings, role);
			DeriveFamilyAndStyle(settings);
			settings.SizeScale = std::clamp(settings.SizeScale, 0.1f, 4.0f);
		}

		if (a_theme.FontRoles[RoleIndex(Menu::FontRole::Body)].File.empty())
			ApplyRoleDefaults(a_theme.FontRoles[RoleIndex(Menu::FontRole::Body)], Menu::FontRole::Body);

		if (!a_theme.FontName.empty())
			a_theme.FontName = NormalizeFontFilePath(a_theme.FontName);

		if (a_theme.FontName.empty())
			a_theme.FontName = a_theme.FontRoles[RoleIndex(Menu::FontRole::Body)].File;
	}

	const FontRoleSettings& GetDefaultRole(FontRole a_role)
	{
		return GetDefaultRoleInternal(a_role);
	}

	ImFontGuard::ImFontGuard(ImFont* a_font)
	{
		if (a_font) {
			ImGui::PushFont(a_font, a_font->LegacySize);
			pushed_ = true;
		}
	}

	ImFontGuard::~ImFontGuard()
	{
		if (pushed_)
			ImGui::PopFont();
	}

	FontRoleGuard::FontRoleGuard(FontRole a_role)
	{
		font_ = Menu::Get().GetFont(a_role);
		if (font_)
			guard_.emplace(font_);
	}

	FontRoleGuard::~FontRoleGuard() = default;

	TabBarPaddingGuard::TabBarPaddingGuard(FontRole a_tabFontRole) :
		originalPadding_(ImGui::GetStyle().FramePadding)
	{
		ImFont* tabFont = Menu::Get().GetFont(a_tabFontRole);
		ImFont* bodyFont = Menu::Get().GetFont(FontRole::Body);
		if (!tabFont || !bodyFont || bodyFont->LegacySize <= 0.0f)
			return;

		const float fontScale = tabFont->LegacySize / bodyFont->LegacySize;
		if (fontScale <= 1.05f)
			return;

		auto& style = ImGui::GetStyle();
		style.FramePadding.x *= fontScale;
		style.FramePadding.y *= fontScale;
		scaled_ = true;
	}

	TabBarPaddingGuard::~TabBarPaddingGuard()
	{
		if (scaled_)
			ImGui::GetStyle().FramePadding = originalPadding_;
	}

	bool BeginTabItemWithFont(const char* a_label, FontRole a_role, ImGuiTabItemFlags a_flags)
	{
		FontRoleGuard guard(a_role);
		return ImGui::BeginTabItem(a_label, nullptr, a_flags);
	}

	void InvalidatePreviewFonts()
	{
		g_previewFontsByFile.clear();
	}

	void AddPreviewFontsToAtlas(float a_previewFontSize)
	{
		InvalidatePreviewFonts();

		const float clampedSize = std::clamp(
			std::round(a_previewFontSize),
			ThemeManager::Constants::MIN_FONT_SIZE,
			ThemeManager::Constants::MAX_FONT_SIZE);

		const auto catalog = fonts::DiscoverFontCatalog();
		const auto fontsRoot = ui::paths::GetFontsPath();
		ImGuiIO& io = ImGui::GetIO();

		ImFontConfig cfg{};
		ThemeManager::InitDefaultFontConfig(cfg);

		std::unordered_set<std::string> seenFiles;
		std::size_t consecutiveFailures = 0;
		bool stop = false;
		for (const auto& family : catalog.families) {
			if (stop)
				break;
			for (const auto& style : family.styles) {
				if (!seenFiles.insert(style.file).second)
					continue;

				const auto fontPath = fontsRoot / style.file;
				if (!ui::paths::IsPathWithinDirectory(fontsRoot, fontPath))
					continue;

				std::error_code ec;
				if (!std::filesystem::exists(fontPath, ec))
					continue;

				if (g_previewFontsByFile.size() >= MAX_PREVIEW_FONTS) {
					stop = true;
					break;
				}

				if (ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), clampedSize, &cfg, g_previewGlyphRanges)) {
					g_previewFontsByFile.emplace(style.file, font);
					consecutiveFailures = 0;
				} else if (++consecutiveFailures >= MAX_PREVIEW_FONT_FAILURES) {
					stop = true;
					break;
				}
			}
		}
	}

	ImFont* GetPreviewFont(const std::string& a_file)
	{
		if (a_file.empty())
			return nullptr;
		const auto it = g_previewFontsByFile.find(a_file);
		return it != g_previewFontsByFile.end() ? it->second : nullptr;
	}

	std::string BuildFontSignature(const Menu::ThemeSettings& a_theme, float a_baseFontSize)
	{
		std::string signature;
		signature.reserve(256);
		for (std::size_t i = 0; i < static_cast<std::size_t>(Menu::FontRole::Count); ++i) {
			const auto role = static_cast<Menu::FontRole>(i);
			const auto& roleSettings = a_theme.FontRoles[i];
			const float scaledSize = std::clamp(a_baseFontSize * roleSettings.SizeScale,
				ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE);
			signature += std::format("{}|{}|{:.2f};", Menu::GetFontRoleKey(role), roleSettings.File, std::round(scaledSize));
		}
		signature += std::format("base|{:.2f};", std::round(a_baseFontSize));
		return signature;
	}
}

namespace cs::fonts
{
	const FamilyInfo* Catalog::FindFamily(const std::string& a_name) const
	{
		const auto it = std::find_if(families.begin(), families.end(), [&a_name](const FamilyInfo& a_info) {
			return ui::IEquals(a_info.name, a_name);
		});
		return it != families.end() ? &(*it) : nullptr;
	}

	const StyleInfo* Catalog::FindStyle(const std::string& a_family, const std::string& a_style) const
	{
		const FamilyInfo* familyInfo = FindFamily(a_family);
		if (!familyInfo)
			return nullptr;

		const auto it = std::find_if(familyInfo->styles.begin(), familyInfo->styles.end(), [&a_style](const StyleInfo& a_info) {
			return ui::IEquals(a_info.style, a_style);
		});
		return it != familyInfo->styles.end() ? &(*it) : nullptr;
	}

	std::string FormatFontDisplayName(const std::string& a_filename)
	{
		return ToDisplayLabel(std::filesystem::path(a_filename).stem().string());
	}

	Catalog DiscoverFontCatalog(bool a_forceRefresh)
	{
		static std::mutex cacheMutex;
		static Catalog cached;
		static bool cacheValid = false;

		std::lock_guard lock(cacheMutex);
		if (cacheValid && !a_forceRefresh)
			return cached;

		Catalog catalog;
		const auto fontsRoot = ui::paths::GetFontsPath();

		std::error_code ec;
		if (std::filesystem::exists(fontsRoot, ec)) {
			for (const auto& entry : std::filesystem::recursive_directory_iterator(fontsRoot, ec)) {
				if (!entry.is_regular_file())
					continue;

				const auto extension = ToLowerCopy(entry.path().extension().string());
				if (extension != ".ttf" && extension != ".otf" && extension != ".ttc")
					continue;

				const auto relativePath = std::filesystem::path(NormalizeRelativeFontPath(fontsRoot, entry.path()));
				const auto familyName = ExtractFamilyName(relativePath);
				const auto styleName = ExtractStyleName(relativePath, familyName);

				auto familyIt = std::find_if(catalog.families.begin(), catalog.families.end(),
					[&familyName](const FamilyInfo& a_info) { return ui::IEquals(a_info.name, familyName); });
				if (familyIt == catalog.families.end()) {
					catalog.families.push_back(FamilyInfo{ familyName, ToDisplayLabel(familyName), {} });
					familyIt = std::prev(catalog.families.end());
				}

				familyIt->styles.push_back(StyleInfo{
					styleName,
					ToDisplayLabel(styleName),
					relativePath.generic_string(),
					familyName });
			}
		}

		std::sort(catalog.families.begin(), catalog.families.end(),
			[](const FamilyInfo& a_lhs, const FamilyInfo& a_rhs) { return a_lhs.name < a_rhs.name; });
		for (auto& family : catalog.families) {
			std::sort(family.styles.begin(), family.styles.end(),
				[](const StyleInfo& a_lhs, const StyleInfo& a_rhs) { return a_lhs.style < a_rhs.style; });
		}

		cached = catalog;
		cacheValid = true;
		return catalog;
	}

	const StyleInfo* FindRegularStyle(const FamilyInfo& a_family)
	{
		const auto it = std::find_if(a_family.styles.begin(), a_family.styles.end(),
			[](const StyleInfo& a_info) { return ui::IEquals(a_info.style, "Regular"); });
		if (it != a_family.styles.end())
			return &(*it);
		return a_family.styles.empty() ? nullptr : &a_family.styles.front();
	}

	int FindFamilyIndex(const Catalog& a_catalog, const std::string& a_familyName)
	{
		for (std::size_t i = 0; i < a_catalog.families.size(); ++i) {
			if (ui::IEquals(a_catalog.families[i].name, a_familyName))
				return static_cast<int>(i);
		}
		return -1;
	}

	int FindStyleIndex(const FamilyInfo& a_family, const std::string& a_styleName)
	{
		for (std::size_t i = 0; i < a_family.styles.size(); ++i) {
			if (ui::IEquals(a_family.styles[i].style, a_styleName))
				return static_cast<int>(i);
		}
		return -1;
	}

	std::vector<std::string> DiscoverFonts()
	{
		std::vector<std::string> files;
		const auto catalog = DiscoverFontCatalog();
		for (const auto& family : catalog.families) {
			for (const auto& style : family.styles)
				files.push_back(style.file);
		}
		return files;
	}

	bool ValidateFont(const std::string& a_fontName)
	{
		if (a_fontName.empty())
			return false;

		const auto fontsRoot = ui::paths::GetFontsPath();
		const auto fontPath = fontsRoot / a_fontName;
		if (!ui::paths::IsPathWithinDirectory(fontsRoot, fontPath))
			return false;

		std::error_code ec;
		return std::filesystem::exists(fontPath, ec);
	}
}
