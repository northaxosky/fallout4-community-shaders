#include "Menu/Theme.h"

#include <filesystem>
#include <string>
#include <string_view>

#include "Log.h"

namespace
{
	auto* L = cs::log::Get("cs.menu.theme");

	namespace palette
	{
		const ImVec4 kText               { 0.90f, 0.94f, 0.90f, 1.00f };
		const ImVec4 kTextDisabled       { 0.52f, 0.58f, 0.52f, 1.00f };
		const ImVec4 kWindowBg           { 0.043f, 0.047f, 0.043f, 0.96f };
		const ImVec4 kChildBg            { 0.043f, 0.047f, 0.043f, 0.52f };
		const ImVec4 kPopupBg            { 0.078f, 0.082f, 0.078f, 0.98f };
		const ImVec4 kSurface            { 0.078f, 0.090f, 0.078f, 1.00f };
		const ImVec4 kSurfaceRaised      { 0.105f, 0.118f, 0.105f, 1.00f };
		const ImVec4 kSurfaceInactive    { 0.060f, 0.064f, 0.060f, 1.00f };
		const ImVec4 kScrollbarGrab      { 0.20f, 0.23f, 0.20f, 1.00f };
		const ImVec4 kTransparent        { 0.00f, 0.00f, 0.00f, 0.00f };
		const ImVec4 kRowAlternate       { 0.20f, 0.28f, 0.20f, 0.12f };
		const ImVec4 kWindowingHighlight { 0.90f, 0.94f, 0.90f, 0.70f };
		const ImVec4 kWindowingDim       { 0.04f, 0.05f, 0.04f, 0.22f };
		const ImVec4 kModalDim           { 0.02f, 0.03f, 0.02f, 0.62f };
	}

	ImVec4 WithAlpha(ImVec4 a_color, float a_alpha)
	{
		a_color.w = a_alpha;
		return a_color;
	}

	std::filesystem::path ResolveBundledFont(std::string_view a_fileName)
	{
		std::filesystem::path path("Data\\F4SE\\Plugins\\FO4CommunityShaders\\Fonts");
		path /= a_fileName;
		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return {};
		return path;
	}

	ImFont* AddFont(ImFontAtlas& a_atlas, const std::filesystem::path& a_path, float a_pointSize)
	{
		ImFontConfig config{};
		config.OversampleH = 3;
		config.OversampleV = 1;
		config.PixelSnapH  = false;
		const std::string path = a_path.string();
		return a_atlas.AddFontFromFileTTF(path.c_str(), a_pointSize, &config);
	}

	cs::theme::Fonts g_fonts;
}

namespace cs::theme
{
	void ApplyDarkTheme(ImGuiStyle& s)
	{
		// Start from dark, then override. Write into the passed style, not the global one.
		ImGui::StyleColorsDark(&s);

		s.WindowRounding    = 6.0f;
		s.ChildRounding     = 4.0f;
		s.FrameRounding     = 4.0f;
		s.PopupRounding     = 4.0f;
		s.ScrollbarRounding = 8.0f;
		s.GrabRounding      = 3.0f;
		s.TabRounding       = 4.0f;

		s.WindowBorderSize  = 1.0f;
		s.FrameBorderSize   = 0.0f;
		s.PopupBorderSize   = 1.0f;
		s.ChildBorderSize   = 1.0f;

		s.WindowPadding     = ImVec2(10.0f, 8.0f);
		s.FramePadding      = ImVec2(8.0f,  4.0f);
		s.ItemSpacing       = ImVec2(8.0f,  5.0f);
		s.ItemInnerSpacing  = ImVec2(6.0f,  4.0f);
		s.IndentSpacing     = 18.0f;
		s.ScrollbarSize     = 12.0f;
		s.GrabMinSize       = 12.0f;

		ImVec4* c = s.Colors;
		c[ImGuiCol_Text]                  = palette::kText;
		c[ImGuiCol_TextDisabled]          = palette::kTextDisabled;

		c[ImGuiCol_WindowBg]              = palette::kWindowBg;
		c[ImGuiCol_ChildBg]               = palette::kChildBg;
		c[ImGuiCol_PopupBg]               = palette::kPopupBg;

		c[ImGuiCol_Border]                = WithAlpha(colors::kAccentDark, 0.45f);
		c[ImGuiCol_BorderShadow]          = palette::kTransparent;

		c[ImGuiCol_FrameBg]               = palette::kSurfaceRaised;
		c[ImGuiCol_FrameBgHovered]        = colors::kAccentDeep;
		c[ImGuiCol_FrameBgActive]         = colors::kAccentDark;

		c[ImGuiCol_TitleBg]               = palette::kSurfaceInactive;
		c[ImGuiCol_TitleBgActive]         = colors::kAccentDeep;
		c[ImGuiCol_TitleBgCollapsed]      = WithAlpha(palette::kSurfaceInactive, 0.78f);

		c[ImGuiCol_MenuBarBg]             = palette::kSurface;

		c[ImGuiCol_ScrollbarBg]           = WithAlpha(palette::kWindowBg, 0.55f);
		c[ImGuiCol_ScrollbarGrab]         = palette::kScrollbarGrab;
		c[ImGuiCol_ScrollbarGrabHovered]  = colors::kAccentDark;
		c[ImGuiCol_ScrollbarGrabActive]   = colors::kAccentMedium;

		c[ImGuiCol_CheckMark]             = colors::kAccent;
		c[ImGuiCol_SliderGrab]            = colors::kAccentMedium;
		c[ImGuiCol_SliderGrabActive]      = colors::kAccent;

		c[ImGuiCol_Button]                = palette::kSurfaceRaised;
		c[ImGuiCol_ButtonHovered]         = colors::kAccentDeep;
		c[ImGuiCol_ButtonActive]          = colors::kAccentDark;

		c[ImGuiCol_Header]                = WithAlpha(colors::kAccentDeep, 0.72f);
		c[ImGuiCol_HeaderHovered]         = colors::kAccentDark;
		c[ImGuiCol_HeaderActive]          = colors::kAccentMedium;

		c[ImGuiCol_Separator]             = WithAlpha(colors::kAccentDark, 0.42f);
		c[ImGuiCol_SeparatorHovered]      = WithAlpha(colors::kAccentMedium, 0.82f);
		c[ImGuiCol_SeparatorActive]       = colors::kAccent;

		c[ImGuiCol_ResizeGrip]            = WithAlpha(palette::kScrollbarGrab, 0.40f);
		c[ImGuiCol_ResizeGripHovered]     = WithAlpha(colors::kAccentMedium, 0.72f);
		c[ImGuiCol_ResizeGripActive]      = colors::kAccent;
		c[ImGuiCol_InputTextCursor]       = colors::kAccent;

		c[ImGuiCol_Tab]                   = palette::kSurface;
		c[ImGuiCol_TabHovered]            = colors::kAccentMedium;
		c[ImGuiCol_TabSelected]           = colors::kAccentDark;
		c[ImGuiCol_TabSelectedOverline]   = colors::kAccent;
		c[ImGuiCol_TabDimmed]             = palette::kSurfaceInactive;
		c[ImGuiCol_TabDimmedSelected]     = WithAlpha(colors::kAccentDeep, 0.72f);
		c[ImGuiCol_TabDimmedSelectedOverline] = colors::kAccentDark;

		c[ImGuiCol_DockingPreview]        = WithAlpha(colors::kAccent, 0.60f);
		c[ImGuiCol_DockingEmptyBg]        = palette::kWindowBg;

		c[ImGuiCol_PlotLines]             = colors::kMuted;
		c[ImGuiCol_PlotLinesHovered]      = colors::kAccent;
		c[ImGuiCol_PlotHistogram]         = colors::kAccent;
		c[ImGuiCol_PlotHistogramHovered]  = colors::kAccentHi;

		c[ImGuiCol_TableHeaderBg]         = WithAlpha(colors::kAccentDeep, 0.72f);
		c[ImGuiCol_TableBorderStrong]     = WithAlpha(colors::kAccentDark, 0.72f);
		c[ImGuiCol_TableBorderLight]      = WithAlpha(colors::kAccentDark, 0.42f);
		c[ImGuiCol_TableRowBg]            = palette::kTransparent;
		c[ImGuiCol_TableRowBgAlt]         = palette::kRowAlternate;

		c[ImGuiCol_TextLink]              = colors::kAccent;
		c[ImGuiCol_TextSelectedBg]        = WithAlpha(colors::kAccentMedium, 0.45f);
		c[ImGuiCol_TreeLines]             = WithAlpha(colors::kAccentDark, 0.52f);
		c[ImGuiCol_DragDropTarget]        = WithAlpha(colors::kAccent, 0.90f);
		c[ImGuiCol_DragDropTargetBg]      = WithAlpha(colors::kAccentDeep, 0.32f);
		c[ImGuiCol_UnsavedMarker]         = colors::kWarning;
		c[ImGuiCol_NavCursor]             = colors::kAccent;
		c[ImGuiCol_NavWindowingHighlight] = palette::kWindowingHighlight;
		c[ImGuiCol_NavWindowingDimBg]     = palette::kWindowingDim;
		c[ImGuiCol_ModalWindowDimBg]      = palette::kModalDim;
	}

	const Fonts& GetFonts() noexcept
	{
		return g_fonts;
	}

	bool LoadFonts(ImGuiIO& io, float a_bodyPointSize)
	{
		constexpr float kBodyPointSize    = 18.0f;
		constexpr float kSubtextPointSize = 15.0f;
		constexpr float kTitlePointSize   = 26.0f;
		constexpr float kHeadingPointSize = 21.0f;
		const float roleScale = a_bodyPointSize / kBodyPointSize;

		g_fonts = {};
		bool loadedAllBundled = true;

		const auto interPath = ResolveBundledFont("Inter-Regular.ttf");
		if (!interPath.empty()) {
			g_fonts.Body    = AddFont(*io.Fonts, interPath, kBodyPointSize * roleScale);
			g_fonts.Subtext = AddFont(*io.Fonts, interPath, kSubtextPointSize * roleScale);
			if (!g_fonts.Body || !g_fonts.Subtext) {
				loadedAllBundled = false;
				L->warn("Failed to load one or more Inter font roles from {}", interPath.string());
			}
		} else {
			loadedAllBundled = false;
			L->warn("Bundled font not found: Data\\F4SE\\Plugins\\FO4CommunityShaders\\Fonts\\Inter-Regular.ttf");
		}

		const auto monoBoldPath = ResolveBundledFont("JetBrainsMono-Bold.ttf");
		if (!monoBoldPath.empty()) {
			g_fonts.Title = AddFont(*io.Fonts, monoBoldPath, kTitlePointSize * roleScale);
			if (!g_fonts.Title) {
				loadedAllBundled = false;
				L->warn("Failed to load JetBrains Mono title font from {}", monoBoldPath.string());
			}
		} else {
			loadedAllBundled = false;
			L->warn("Bundled font not found: Data\\F4SE\\Plugins\\FO4CommunityShaders\\Fonts\\JetBrainsMono-Bold.ttf");
		}

		const auto monoRegularPath = ResolveBundledFont("JetBrainsMono-Regular.ttf");
		if (!monoRegularPath.empty()) {
			g_fonts.Heading = AddFont(*io.Fonts, monoRegularPath, kHeadingPointSize * roleScale);
			if (!g_fonts.Heading) {
				loadedAllBundled = false;
				L->warn("Failed to load JetBrains Mono heading font from {}", monoRegularPath.string());
			}
		} else {
			loadedAllBundled = false;
			L->warn("Bundled font not found: Data\\F4SE\\Plugins\\FO4CommunityShaders\\Fonts\\JetBrainsMono-Regular.ttf");
		}

		ImFont* fallback = nullptr;
		const auto useFallback = [&](ImFont*& a_font) {
			if (a_font)
				return;
			if (!fallback)
				fallback = io.Fonts->AddFontDefault();
			a_font = fallback;
		};
		useFallback(g_fonts.Body);
		useFallback(g_fonts.Subtext);
		useFallback(g_fonts.Title);
		useFallback(g_fonts.Heading);

		io.FontDefault = g_fonts.Body;
		if (loadedAllBundled) {
			L->info(
				"Loaded font roles: Inter body {:.0f}pt/subtext {:.0f}pt; JetBrains Mono title {:.0f}pt/heading {:.0f}pt",
				kBodyPointSize * roleScale,
				kSubtextPointSize * roleScale,
				kTitlePointSize * roleScale,
				kHeadingPointSize * roleScale);
		} else {
			L->warn("Using ImGui built-in ProggyClean for missing font roles");
		}
		return loadedAllBundled;
	}
}
