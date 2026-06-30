#pragma once

#include <imgui.h>

namespace cs::theme
{
	// Modern dark theme with Fallout-amber accent (Pip-Boy adjacent). Slight window
	// transparency (alpha 0.94) so the game frame reads through; popups/modals/toast
	// stay opaque so confirmation dialogs read clean.
	void ApplyDarkTheme(ImGuiStyle& a_style);

	// Load body font (resolves %WINDIR%\Fonts\segoeui.ttf). Returns true if a TTF was
	// loaded; false means ImGui falls back to its built-in ProggyClean. Caller should
	// only invoke this once per ImGui context (typically right after CreateContext +
	// before the first NewFrame).
	bool LoadFonts(ImGuiIO& a_io, float a_bodyPointSize = 18.0f);

	// Semantic color palette. Features should use these instead of hard-coding ImVec4
	// literals for status text so the look stays coherent if the theme is tweaked.
	namespace colors
	{
		inline const ImVec4 kAccent   { 1.00f, 0.72f, 0.32f, 1.00f };  // amber
		inline const ImVec4 kAccentHi { 1.00f, 0.82f, 0.48f, 1.00f };  // brighter amber
		inline const ImVec4 kSuccess  { 0.50f, 0.90f, 0.55f, 1.00f };  // mint green
		inline const ImVec4 kWarning  { 1.00f, 0.78f, 0.42f, 1.00f };  // soft amber (warning)
		inline const ImVec4 kError    { 1.00f, 0.42f, 0.42f, 1.00f };  // coral red
		inline const ImVec4 kInfo     { 0.55f, 0.80f, 1.00f, 1.00f };  // sky blue
		inline const ImVec4 kMuted    { 0.65f, 0.65f, 0.70f, 1.00f };  // dimmed text
	}
}
