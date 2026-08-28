#include "Menu/ThemeManager.h"

#include "Log.h"
#include "Menu/BackgroundBlur.h"
#include "Menu/Fonts.h"
#include "Menu/Menu.h"
#include "Settings/TomlUtil.h"
#include "Utils/UI.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>
#include <unordered_map>

#include <imgui_impl_dx11.h>
#include <imgui_internal.h>
#include <wrl/client.h>

namespace
{
	auto* L = cs::log::Get("menu");
}

namespace cs
{
	ThemeManager& ThemeManager::Get()
	{
		static ThemeManager instance;
		return instance;
	}

	void ThemeManager::SetupImGuiStyle(const Menu& a_menu)
	{
		auto& style = ImGui::GetStyle();
		auto& colors = style.Colors;

		const auto& themeSettings = a_menu.GetTheme();

		auto styleCopy = themeSettings.Style;

		float globalScale = themeSettings.GlobalScale;
		if (std::abs(globalScale - Constants::DEFAULT_GLOBAL_SCALE) < 0.001f)
			globalScale = Constants::DEFAULT_GLOBAL_SCALE;

		// Theme style values target the 1080p baseline; rescale for the live font size.
		float fontScale = 1.0f;
		auto& io = ImGui::GetIO();
		if (io.FontDefault) {
			constexpr float kBaselineFontSize = Constants::DEFAULT_SCREEN_HEIGHT * Constants::DEFAULT_FONT_RATIO;
			fontScale = io.FontDefault->LegacySize / kBaselineFontSize;
		}
		const float scaleFactor = fontScale * std::exp2(globalScale);
		styleCopy.ScaleAllSizes(scaleFactor);

		// ScaleAllSizes skips border sizes; floor non-zero results at 1px.
		auto scaleSize = [scaleFactor](float a_value) -> float {
			if (a_value <= 0.0f)
				return 0.0f;
			return ImMax(1.0f, ImTrunc(a_value * scaleFactor));
		};
		styleCopy.WindowBorderSize = scaleSize(themeSettings.Style.WindowBorderSize);
		styleCopy.ChildBorderSize = scaleSize(themeSettings.Style.ChildBorderSize);
		styleCopy.PopupBorderSize = scaleSize(themeSettings.Style.PopupBorderSize);
		styleCopy.FrameBorderSize = scaleSize(themeSettings.Style.FrameBorderSize);
		styleCopy.TabBorderSize = scaleSize(themeSettings.Style.TabBorderSize);
		styleCopy.TabBarBorderSize = scaleSize(themeSettings.Style.TabBarBorderSize);
		styleCopy.SeparatorTextBorderSize = scaleSize(themeSettings.Style.SeparatorTextBorderSize);
		styleCopy.DockingSeparatorSize = scaleSize(themeSettings.Style.DockingSeparatorSize);
		styleCopy.MouseCursorScale = ImMax(1.0f, themeSettings.Style.MouseCursorScale);

		style = styleCopy;
		style.HoverDelayNormal = themeSettings.TooltipHoverDelay;
		style.FontScaleMain = std::exp2(globalScale);

		for (std::size_t i = 0; i < std::min(themeSettings.FullPalette.size(), static_cast<std::size_t>(ImGuiCol_COUNT)); ++i)
			colors[i] = themeSettings.FullPalette[i];

		colors[ImGuiCol_ScrollbarBg].w = themeSettings.ScrollbarOpacity.Background;
		colors[ImGuiCol_ScrollbarGrab].w = themeSettings.ScrollbarOpacity.Thumb;
		colors[ImGuiCol_ScrollbarGrabHovered].w = themeSettings.ScrollbarOpacity.ThumbHovered;
		colors[ImGuiCol_ScrollbarGrabActive].w = themeSettings.ScrollbarOpacity.ThumbActive;
	}

	void ThemeManager::ForceApplyDefaultTheme()
	{
		toml::table defaultTheme;
		if (!Get().LoadTheme("Default", defaultTheme)) {
			L->warn("ForceApplyDefaultTheme: could not load the Default theme");
			return;
		}

		std::array<ImVec4, ImGuiCol_COUNT> palette{};
		Menu::PaletteFromToml(defaultTheme, palette);

		auto& colors = ImGui::GetStyle().Colors;
		for (int i = 0; i < ImGuiCol_COUNT; ++i)
			colors[i] = palette[static_cast<std::size_t>(i)];
	}

	void ThemeManager::InitDefaultFontConfig(ImFontConfig& a_config)
	{
		a_config = {};
		a_config.OversampleH = Constants::FCONF_OVERSAMPLE_H;
		a_config.OversampleV = Constants::FCONF_OVERSAMPLE_V;
		a_config.PixelSnapH = Constants::FCONF_PIXELSNAP_H;
		a_config.RasterizerMultiply = Constants::FCONF_RASTERIZER_MULTIPLY;
	}

	bool ThemeManager::ReloadFont(const Menu& a_menu, float& a_cachedFontSize)
	{
		static std::atomic<bool> isReloading{ false };
		bool expected = false;
		if (!isReloading.compare_exchange_strong(expected, true))
			return false;

		struct ReloadGuard
		{
			std::atomic<bool>& flag;
			explicit ReloadGuard(std::atomic<bool>& a_flag) :
				flag(a_flag) {}
			~ReloadGuard() { flag = false; }
		} guard(isReloading);

		auto& menu = const_cast<Menu&>(a_menu);
		const auto& themeSettings = a_menu.GetTheme();
		ImGuiIO& io = ImGui::GetIO();

		ImGuiContext* ctx = ImGui::GetCurrentContext();
		if (!ctx) {
			L->error("ReloadFont: no valid ImGui context");
			return false;
		}
		if (ctx->WithinFrameScope) {
			L->error("ReloadFont: cannot reload font within frame scope");
			return false;
		}
		if (!io.Fonts) {
			L->error("ReloadFont: no font atlas available");
			return false;
		}

		auto* device = menu.GetDevice();
		auto* context = menu.GetContext();
		if (!device || !context) {
			L->error("ReloadFont: D3D11 device or context is null");
			return false;
		}

		auto invalidatePublishedFonts = [&]() {
			menu.loadedFontRoles.fill(nullptr);
			io.FontDefault = nullptr;
		};
		auto resetAtlas = [&]() {
			io.Fonts->Clear();
			MenuFonts::InvalidatePreviewFonts();
			invalidatePublishedFonts();
			io.Fonts->TexGlyphPadding = 1;
		};

		resetAtlas();

		ImFontConfig fontConfig;
		InitDefaultFontConfig(fontConfig);

		const float fontSize = ResolveFontSize(a_menu);
		const auto fontsRoot = ui::paths::GetFontsPath();
		constexpr auto roleCount = static_cast<std::size_t>(Menu::FontRole::Count);
		std::array<ImFont*, roleCount> loadedFontRoles{};
		auto resolvedFontRoles = themeSettings.FontRoles;
		auto cachedFontFilesByRole = menu.cachedFontFilesByRole;
		auto cachedFontPixelSizesByRole = menu.cachedFontPixelSizesByRole;

		std::unordered_map<std::string, ImFont*> atlasCache;
		std::vector<std::size_t> rolesNeedingFallback;
		bool usingEmergencyFont = false;

		for (std::size_t i = 0; i < static_cast<std::size_t>(Menu::FontRole::Count); ++i) {
			const auto role = static_cast<Menu::FontRole>(i);
			Menu::ThemeSettings::FontRoleSettings effective = themeSettings.FontRoles[i];

			if (effective.SizeScale <= 0.0f)
				effective.SizeScale = Menu::GetFontRoleDefaultScale(role);
			if (effective.File.empty())
				effective = Menu::GetDefaultFontRole(role);

			const float scaledSize = std::clamp(fontSize * effective.SizeScale, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
			const float roundedSize = std::round(scaledSize);
			cachedFontPixelSizesByRole[i] = roundedSize;

			ImFont* loadedFont = nullptr;
			if (!effective.File.empty()) {
				auto fontPath = fontsRoot / effective.File;

				// Reject traversal out of the fonts directory before touching disk.
				if (!ui::paths::IsPathWithinDirectory(fontsRoot, fontPath)) {
					L->error("Font path traversal attempt for role '{}': {}", Menu::GetFontRoleKey(role), effective.File);
					effective = Menu::GetDefaultFontRole(role);
					fontPath = fontsRoot / effective.File;
				}

				std::error_code ec;
				if (std::filesystem::exists(fontPath, ec)) {
					const std::string cacheKey = std::format("{}|{}", effective.File, static_cast<int>(roundedSize));
					const auto cached = atlasCache.find(cacheKey);
					if (cached != atlasCache.end()) {
						loadedFont = cached->second;
					} else {
						ImFontConfig cfg = fontConfig;
						if (auto* font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), roundedSize, &cfg)) {
							atlasCache.emplace(cacheKey, font);
							loadedFont = font;
						}
					}
				}
			}

			if (!loadedFont) {
				rolesNeedingFallback.push_back(i);
			} else {
				loadedFontRoles[i] = loadedFont;
				resolvedFontRoles[i] = effective;
				cachedFontFilesByRole[i] = effective.File;
			}
		}

		constexpr auto bodyIndex = static_cast<std::size_t>(Menu::FontRole::Body);
		if (!loadedFontRoles[bodyIndex]) {
			const auto& defaults = Menu::GetDefaultFontRole(Menu::FontRole::Body);
			const float bodySize = std::clamp(fontSize * defaults.SizeScale, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
			const float roundedBodySize = std::round(bodySize);
			cachedFontPixelSizesByRole[bodyIndex] = roundedBodySize;

			ImFont* fallbackBody = nullptr;
			const auto defaultPath = fontsRoot / defaults.File;
			std::error_code ec;
			if (std::filesystem::exists(defaultPath, ec)) {
				ImFontConfig cfg = fontConfig;
				fallbackBody = io.Fonts->AddFontFromFileTTF(defaultPath.string().c_str(), roundedBodySize, &cfg);
				if (fallbackBody)
					atlasCache.emplace(std::format("{}|{}", defaults.File, static_cast<int>(roundedBodySize)), fallbackBody);
			}
			if (!fallbackBody)
				fallbackBody = io.Fonts->AddFontDefault();

			loadedFontRoles[bodyIndex] = fallbackBody;
			resolvedFontRoles[bodyIndex] = defaults;
			cachedFontFilesByRole[bodyIndex] = defaults.File;
		}

		ImFont* bodyFont = loadedFontRoles[bodyIndex];
		for (const std::size_t idx : rolesNeedingFallback) {
			if (idx == bodyIndex)
				continue;
			const auto role = static_cast<Menu::FontRole>(idx);
			const auto& defaults = Menu::GetDefaultFontRole(role);
			const float fallbackSize = std::clamp(fontSize * defaults.SizeScale, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
			cachedFontPixelSizesByRole[idx] = std::round(fallbackSize);
			loadedFontRoles[idx] = bodyFont;
			resolvedFontRoles[idx] = defaults;
			cachedFontFilesByRole[idx] = defaults.File;
		}

		if (!bodyFont) {
			bodyFont = io.Fonts->AddFontDefault();
			loadedFontRoles[bodyIndex] = bodyFont;
		}

		if (menu.buildFontPreviewAtlas)
			MenuFonts::AddPreviewFontsToAtlas(cachedFontPixelSizesByRole[bodyIndex]);

		if (!io.Fonts->Build()) {
			L->error("ReloadFont: failed to build font atlas");

			resetAtlas();
			ImFont* emergencyFont = io.Fonts->AddFontDefault();
			if (emergencyFont && io.Fonts->Build()) {
				loadedFontRoles.fill(emergencyFont);
				bodyFont = emergencyFont;
				usingEmergencyFont = true;
				menu.buildFontPreviewAtlas = false;
			} else {
				L->error("ReloadFont: emergency fallback failed");
				return false;
			}
		}

		// Wait for the GPU to drain before invalidating the atlas texture.
		context->Flush();

		Microsoft::WRL::ComPtr<ID3D11Query> eventQuery;
		D3D11_QUERY_DESC queryDesc{ D3D11_QUERY_EVENT, 0 };
		if (SUCCEEDED(device->CreateQuery(&queryDesc, eventQuery.GetAddressOf()))) {
			context->End(eventQuery.Get());
			BOOL queryData = FALSE;
			for (int i = 0; i < 1000 && context->GetData(eventQuery.Get(), &queryData, sizeof(BOOL), 0) != S_OK; ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		ImGui_ImplDX11_InvalidateDeviceObjects();

		if (!ImGui_ImplDX11_CreateDeviceObjects()) {
			L->error("ReloadFont: failed to create device objects");

			ImGui_ImplDX11_InvalidateDeviceObjects();
			resetAtlas();
			ImFont* emergencyFont = io.Fonts->AddFontDefault();

			bool recovered = false;
			if (emergencyFont && io.Fonts->Build()) {
				if (ImGui_ImplDX11_CreateDeviceObjects()) {
					loadedFontRoles.fill(emergencyFont);
					bodyFont = emergencyFont;
					usingEmergencyFont = true;
					menu.buildFontPreviewAtlas = false;
					recovered = true;
				}
			}

			if (!recovered) {
				L->error("ReloadFont: unable to recover device objects");
				return false;
			}
		}

		if (!io.Fonts->TexIsBuilt) {
			L->error("ReloadFont: font texture not created");
			ImGui_ImplDX11_InvalidateDeviceObjects();
			resetAtlas();
			return false;
		}

		menu.loadedFontRoles = loadedFontRoles;
		menu.cachedFontFilesByRole = std::move(cachedFontFilesByRole);
		menu.cachedFontPixelSizesByRole = cachedFontPixelSizesByRole;
		menu.GetSettings().Theme.FontRoles = std::move(resolvedFontRoles);
		menu.cachedFontName = usingEmergencyFont ?
		                          "ImGui Default" :
		                          menu.GetFontRoleSettings(Menu::FontRole::Body).File;
		menu.GetSettings().Theme.FontName = menu.GetFontRoleSettings(Menu::FontRole::Body).File;
		menu.cachedFontSignature = menu.BuildFontSignature(fontSize);
		io.FontDefault = bodyFont;

		float globalScale = themeSettings.GlobalScale;
		if (std::abs(globalScale - Constants::DEFAULT_GLOBAL_SCALE) < 0.001f)
			globalScale = Constants::DEFAULT_GLOBAL_SCALE;

		ImGui::GetStyle().FontScaleMain = std::exp2(globalScale);
		// Zeroing forces UpdateFontsNewFrame to re-derive the base size from the font.
		ImGui::GetStyle().FontSizeBase = 0.0f;

		a_cachedFontSize = fontSize;
		menu.cachedFontSize = fontSize;

		return true;
	}

	float ThemeManager::ResolveFontSize(const Menu& a_menu)
	{
		const auto& settings = a_menu.GetSettings();

		if (!settings.UseResolutionFont) {
			const float configured = settings.Theme.FontSize;
			if (std::round(configured) > 0)
				return std::clamp(configured, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
		}

		float screenHeight = static_cast<float>(a_menu.GetBackbufferHeight());
		if (screenHeight <= 0.0f)
			screenHeight = ImGui::GetIO().DisplaySize.y;
		if (screenHeight <= 0.0f)
			screenHeight = Constants::DEFAULT_SCREEN_HEIGHT;

		return std::clamp(screenHeight * Constants::DEFAULT_FONT_RATIO, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
	}

	std::filesystem::path ThemeManager::GetThemesDirectory() const
	{
		return ui::paths::GetThemesPath();
	}

	std::size_t ThemeManager::DiscoverThemes()
	{
		if (discovered)
			return themes.size();

		themes.clear();

		const auto themesDir = GetThemesDirectory();
		std::error_code ec;
		if (!std::filesystem::exists(themesDir, ec)) {
			std::filesystem::create_directories(themesDir, ec);
			if (ec)
				L->warn("Could not create themes directory '{}': {}", themesDir.string(), ec.message());
		}

		CreateDefaultThemeFiles();

		if (std::filesystem::exists(themesDir, ec)) {
			for (const auto& entry : std::filesystem::directory_iterator(themesDir, ec)) {
				if (themes.size() >= MAX_THEMES) {
					L->warn("Theme discovery stopped at {} themes", MAX_THEMES);
					break;
				}
				if (!entry.is_regular_file() || entry.path().extension() != ".toml")
					continue;
				if (auto info = LoadThemeFile(entry.path()))
					themes.push_back(std::move(*info));
			}
		}

		std::sort(themes.begin(), themes.end(), [](const ThemeInfo& a_lhs, const ThemeInfo& a_rhs) {
			if (a_lhs.name == "Default" && a_rhs.name != "Default")
				return true;
			if (a_rhs.name == "Default")
				return false;
			return a_lhs.name < a_rhs.name;
		});

		discovered = true;
		L->info("Discovered {} menu themes", themes.size());
		return themes.size();
	}

	std::vector<std::string> ThemeManager::GetThemeNames() const
	{
		std::vector<std::string> names;
		names.reserve(themes.size());
		for (const auto& theme : themes) {
			if (theme.isValid)
				names.push_back(theme.name);
		}
		return names;
	}

	bool ThemeManager::LoadTheme(const std::string& a_themeName, toml::table& a_themeSettings)
	{
		if (!discovered)
			DiscoverThemes();

		const auto* info = GetThemeInfo(a_themeName);
		if (!info || !info->isValid)
			return false;

		const auto* themeNode = info->themeData.get("theme");
		if (!themeNode || !themeNode->is_table())
			return false;

		a_themeSettings = *themeNode->as_table();
		return true;
	}

	bool ThemeManager::SaveTheme(const std::string& a_themeName,
		const toml::table& a_themeSettings,
		const std::string& a_displayName,
		const std::string& a_description)
	{
		if (a_themeName.empty())
			return false;

		const auto themesDir = GetThemesDirectory();
		std::error_code ec;
		std::filesystem::create_directories(themesDir, ec);

		const auto path = themesDir / (a_themeName + ".toml");
		if (!ui::paths::IsPathWithinDirectory(themesDir, path)) {
			L->error("Refusing to save theme outside the themes directory: {}", a_themeName);
			return false;
		}

		toml::table root;
		root.insert_or_assign("display_name", a_displayName.empty() ? a_themeName : a_displayName);
		root.insert_or_assign("description", a_description);
		root.insert_or_assign("version", std::string("1.0.0"));
		root.insert_or_assign("theme", a_themeSettings);

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) {
			L->warn("Failed to open theme '{}' for writing", path.string());
			return false;
		}
		file << root;
		if (!file) {
			L->warn("Failed to write theme '{}'", path.string());
			return false;
		}

		RefreshThemes();
		return true;
	}

	const ThemeManager::ThemeInfo* ThemeManager::GetThemeInfo(const std::string& a_themeName) const
	{
		const auto it = std::find_if(themes.begin(), themes.end(), [&a_themeName](const ThemeInfo& a_info) {
			return a_info.name == a_themeName;
		});
		return it != themes.end() ? &(*it) : nullptr;
	}

	void ThemeManager::RefreshThemes()
	{
		discovered = false;
		DiscoverThemes();
	}

	bool ThemeManager::IsPresetTheme(const std::string& a_themeName) const
	{
		return a_themeName == "Default" || a_themeName == "Light";
	}

	void ThemeManager::CreateDefaultThemeFiles()
	{
		const auto themesDir = GetThemesDirectory();
		std::error_code ec;
		std::filesystem::create_directories(themesDir, ec);

		const auto defaultPath = themesDir / "Default.toml";
		if (std::filesystem::exists(defaultPath, ec))
			return;

		toml::table theme;
		Menu::ThemeToToml(Menu::ThemeSettings{}, theme);

		toml::table root;
		root.insert_or_assign("display_name", std::string("Default"));
		root.insert_or_assign("description", std::string("Community Shaders default dark theme"));
		root.insert_or_assign("version", std::string("1.0.0"));
		root.insert_or_assign("theme", theme);

		std::ofstream file(defaultPath, std::ios::binary | std::ios::trunc);
		if (!file) {
			L->warn("Failed to create the default theme file");
			return;
		}
		file << root;
	}

	std::unique_ptr<ThemeManager::ThemeInfo> ThemeManager::LoadThemeFile(const std::filesystem::path& a_filePath)
	{
		std::error_code ec;
		const auto size = std::filesystem::file_size(a_filePath, ec);
		if (ec || size > MAX_FILE_SIZE) {
			L->warn("Skipping theme '{}': unreadable or larger than {} bytes", a_filePath.string(), MAX_FILE_SIZE);
			return nullptr;
		}

		toml::table root;
		try {
			root = toml::parse_file(a_filePath.string());
		} catch (const toml::parse_error& e) {
			L->warn("Skipping theme '{}': {}", a_filePath.string(), std::string(e.description()));
			return nullptr;
		}

		auto info = std::make_unique<ThemeInfo>();
		info->name = a_filePath.stem().string();
		info->filePath = a_filePath.string();
		info->themeData = std::move(root);
		info->isValid = ValidateThemeData(info->themeData);
		if (!info->isValid) {
			L->warn("Skipping theme '{}': missing [theme] table", a_filePath.string());
			return nullptr;
		}

		info->displayName = toml_util::ReadString(info->themeData, "display_name", info->name, "theme");
		info->description = toml_util::ReadString(info->themeData, "description", "", "theme");
		info->version = toml_util::ReadString(info->themeData, "version", "", "theme");
		info->author = toml_util::ReadString(info->themeData, "author", "", "theme");
		return info;
	}

	bool ThemeManager::ValidateThemeData(const toml::table& a_themeData) const
	{
		const auto* node = a_themeData.get("theme");
		return node != nullptr && node->is_table();
	}
}
