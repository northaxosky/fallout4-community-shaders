#pragma once

#include <imgui.h>

namespace cs::theme
{
	// Dark Fallout-amber theme; windows are slightly transparent, popups/modals/toasts stay opaque.
	void ApplyDarkTheme(ImGuiStyle& a_style);

	// Loads %WINDIR%\Fonts\segoeui.ttf once per ImGui context; false means ProggyClean fallback.
	bool LoadFonts(ImGuiIO& a_io, float a_bodyPointSize = 18.0f);

	// Shared status palette; use these instead of hard-coded ImVec4 literals.
	namespace colors
	{
		inline const ImVec4 kAccent   { 1.00f, 0.72f, 0.32f, 1.00f };
		inline const ImVec4 kAccentHi { 1.00f, 0.82f, 0.48f, 1.00f };
		inline const ImVec4 kSuccess  { 0.50f, 0.90f, 0.55f, 1.00f };
		inline const ImVec4 kWarning  { 1.00f, 0.78f, 0.42f, 1.00f };
		inline const ImVec4 kError    { 1.00f, 0.42f, 0.42f, 1.00f };
		inline const ImVec4 kInfo     { 0.55f, 0.80f, 1.00f, 1.00f };
		inline const ImVec4 kMuted    { 0.65f, 0.65f, 0.70f, 1.00f };
	}
}
