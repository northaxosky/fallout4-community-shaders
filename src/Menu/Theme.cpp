#include "Menu/Theme.h"

#include <Windows.h>

#include <filesystem>
#include <string>

#include "Log.h"

namespace
{
	auto* L = cs::log::Get("cs.menu.theme");

	std::string ResolveSystemFont(const wchar_t* a_fileName)
	{
		wchar_t buf[MAX_PATH]{};
		const UINT n = ::GetWindowsDirectoryW(buf, MAX_PATH);
		if (n == 0 || n >= MAX_PATH)
			return {};

		std::filesystem::path p(buf);
		p /= L"Fonts";
		p /= a_fileName;

		std::error_code ec;
		if (!std::filesystem::exists(p, ec))
			return {};

		return p.string();
	}
}

namespace cs::theme
{
	void ApplyDarkTheme(ImGuiStyle& s)
	{
		// Start from dark, then override.
		ImGui::StyleColorsDark();

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
		c[ImGuiCol_Text]                  = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
		c[ImGuiCol_TextDisabled]          = ImVec4(0.55f, 0.55f, 0.60f, 1.00f);

		c[ImGuiCol_WindowBg]              = ImVec4(0.10f, 0.10f, 0.12f, 0.94f);
		c[ImGuiCol_ChildBg]               = ImVec4(0.08f, 0.08f, 0.10f, 0.50f);
		c[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);

		c[ImGuiCol_Border]                = ImVec4(0.20f, 0.20f, 0.24f, 0.55f);
		c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

		c[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
		c[ImGuiCol_FrameBgHovered]        = ImVec4(0.18f, 0.22f, 0.20f, 1.00f);
		c[ImGuiCol_FrameBgActive]         = ImVec4(0.16f, 0.30f, 0.18f, 1.00f);

		c[ImGuiCol_TitleBg]               = ImVec4(0.08f, 0.10f, 0.09f, 1.00f);
		c[ImGuiCol_TitleBgActive]         = ImVec4(0.10f, 0.20f, 0.12f, 1.00f);
		c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.08f, 0.10f, 0.09f, 0.75f);

		c[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

		c[ImGuiCol_ScrollbarBg]           = ImVec4(0.06f, 0.06f, 0.08f, 0.50f);
		c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
		c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.28f, 0.40f, 0.30f, 1.00f);
		c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.30f, 0.60f, 0.36f, 1.00f);

		c[ImGuiCol_CheckMark]             = ImVec4(0.40f, 1.00f, 0.50f, 1.00f);
		c[ImGuiCol_SliderGrab]            = ImVec4(0.32f, 0.92f, 0.40f, 1.00f);
		c[ImGuiCol_SliderGrabActive]      = ImVec4(0.50f, 1.00f, 0.62f, 1.00f);

		c[ImGuiCol_Button]                = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
		c[ImGuiCol_ButtonHovered]         = ImVec4(0.18f, 0.30f, 0.20f, 1.00f);
		c[ImGuiCol_ButtonActive]          = ImVec4(0.20f, 0.45f, 0.24f, 1.00f);

		c[ImGuiCol_Header]                = ImVec4(0.12f, 0.18f, 0.14f, 1.00f);
		c[ImGuiCol_HeaderHovered]         = ImVec4(0.16f, 0.28f, 0.18f, 1.00f);
		c[ImGuiCol_HeaderActive]          = ImVec4(0.22f, 0.40f, 0.26f, 1.00f);

		c[ImGuiCol_Separator]             = ImVec4(0.24f, 0.22f, 0.22f, 0.65f);
		c[ImGuiCol_SeparatorHovered]      = ImVec4(0.30f, 0.60f, 0.36f, 0.80f);
		c[ImGuiCol_SeparatorActive]       = ImVec4(0.40f, 0.95f, 0.50f, 1.00f);

		c[ImGuiCol_ResizeGrip]            = ImVec4(0.30f, 0.30f, 0.34f, 0.40f);
		c[ImGuiCol_ResizeGripHovered]     = ImVec4(0.30f, 0.60f, 0.36f, 0.70f);
		c[ImGuiCol_ResizeGripActive]      = ImVec4(0.40f, 0.95f, 0.50f, 1.00f);

		c[ImGuiCol_Tab]                   = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
		c[ImGuiCol_TabHovered]            = ImVec4(0.18f, 0.30f, 0.20f, 1.00f);
		c[ImGuiCol_TabActive]             = ImVec4(0.18f, 0.30f, 0.20f, 1.00f);
		c[ImGuiCol_TabUnfocused]          = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
		c[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.12f, 0.16f, 0.13f, 1.00f);

		c[ImGuiCol_DockingPreview]        = ImVec4(0.32f, 0.92f, 0.40f, 0.60f);
		c[ImGuiCol_DockingEmptyBg]        = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

		c[ImGuiCol_PlotLines]             = ImVec4(0.65f, 0.65f, 0.70f, 1.00f);
		c[ImGuiCol_PlotLinesHovered]      = ImVec4(0.50f, 1.00f, 0.62f, 1.00f);
		c[ImGuiCol_PlotHistogram]         = ImVec4(0.32f, 0.92f, 0.40f, 1.00f);
		c[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.55f, 1.00f, 0.65f, 1.00f);

		c[ImGuiCol_TableHeaderBg]         = ImVec4(0.12f, 0.18f, 0.14f, 1.00f);
		c[ImGuiCol_TableBorderStrong]     = ImVec4(0.22f, 0.28f, 0.24f, 1.00f);
		c[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.22f, 0.20f, 1.00f);
		c[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);

		c[ImGuiCol_TextSelectedBg]        = ImVec4(0.40f, 0.95f, 0.50f, 0.45f);
		c[ImGuiCol_DragDropTarget]        = ImVec4(0.50f, 1.00f, 0.62f, 0.90f);
		c[ImGuiCol_NavHighlight]          = ImVec4(0.32f, 0.92f, 0.40f, 1.00f);
		c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.10f, 0.10f, 0.10f, 0.20f);
		c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.05f, 0.05f, 0.07f, 0.55f);
	}

	bool LoadFonts(ImGuiIO& io, float a_bodyPointSize)
	{
		const std::string segoePath = ResolveSystemFont(L"segoeui.ttf");
		if (!segoePath.empty()) {
			ImFontConfig cfg{};
			cfg.OversampleH = 3;
			cfg.OversampleV = 1;
			cfg.PixelSnapH  = false;
			ImFont* f = io.Fonts->AddFontFromFileTTF(segoePath.c_str(), a_bodyPointSize, &cfg);
			if (f) {
				io.FontDefault = f;
				L->info("Loaded Segoe UI ({:.0f}pt) from {}", a_bodyPointSize, segoePath);
				return true;
			}
		}

		L->warn("System font segoeui.ttf not found; falling back to ImGui built-in ProggyClean");
		io.Fonts->AddFontDefault();
		return false;
	}
}
