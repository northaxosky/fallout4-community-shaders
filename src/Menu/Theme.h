#pragma once

#include <imgui.h>

namespace cs::theme
{
	// Dark Pip-Boy theme; windows are slightly transparent, popups/modals/toasts stay opaque.
	void ApplyDarkTheme(ImGuiStyle& a_style);

	struct Fonts
	{
		ImFont* Body    = nullptr;
		ImFont* Subtext = nullptr;
		ImFont* Title   = nullptr;
		ImFont* Heading = nullptr;
	};

	const Fonts& GetFonts() noexcept;

	// Loads bundled role fonts once per ImGui context; false means at least one ProggyClean fallback.
	bool LoadFonts(ImGuiIO& a_io, float a_bodyPointSize = 18.0f);

	// Shared Pip-Boy and status palette; use these instead of hard-coded ImVec4 literals.
	namespace colors
	{
		inline const ImVec4 kAccent       { 0.000f, 0.933f, 0.000f, 1.00f };
		inline const ImVec4 kAccentHi     = kAccent;
		inline const ImVec4 kAccentMedium { 0.000f, 0.557f, 0.000f, 1.00f };
		inline const ImVec4 kAccentDark   { 0.000f, 0.373f, 0.000f, 1.00f };
		inline const ImVec4 kAccentDeep   { 0.000f, 0.184f, 0.000f, 1.00f };
		inline const ImVec4 kTextOnAccent { 0.000f, 0.000f, 0.000f, 1.00f };
		inline const ImVec4 kSuccess  { 0.50f, 0.90f, 0.55f, 1.00f };
		inline const ImVec4 kWarning  { 1.00f, 0.78f, 0.42f, 1.00f };
		inline const ImVec4 kError    { 1.00f, 0.42f, 0.42f, 1.00f };
		inline const ImVec4 kInfo     { 0.55f, 0.80f, 1.00f, 1.00f };
		inline const ImVec4 kMuted    { 0.65f, 0.65f, 0.70f, 1.00f };
	}
}
