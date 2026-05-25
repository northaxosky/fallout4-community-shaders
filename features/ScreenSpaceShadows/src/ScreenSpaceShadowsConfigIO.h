#pragma once

#include "ScreenSpaceShadows.h"

#include <toml++/toml.hpp>

namespace cs::features::sss
{
	// Parses [settings] subtable into a_out. Missing keys leave a_out unchanged (caller resets to
	// Settings{} when full snapshot semantics are wanted). Unknown keys silently ignored. The
	// `preset` field is treated as an ordinary clamped int here (no first-launch bootstrap; that
	// is owned by ScreenSpaceShadows::LoadSettings, not by preset apply).
	void ParseSettings(const toml::table& a_root, ScreenSpaceShadows::Settings& a_out);

	// Emits canonical visual-look fields into [settings] (insert-or-assign on a_root). Excludes
	// preview_scale / show_preview; those are runtime-only debug UI scratch.
	void EmitSettings(toml::table& a_root, const ScreenSpaceShadows::Settings& a_settings);
}
