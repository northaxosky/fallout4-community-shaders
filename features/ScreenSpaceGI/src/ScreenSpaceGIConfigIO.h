#pragma once

#include "ScreenSpaceGI.h"

#include <toml++/toml.hpp>

namespace cs::features::ssgi
{
	// Parses [settings] subtable into a_out. Missing keys leave a_out unchanged. Unknown keys
	// silently ignored. The `preset` field is treated as an ordinary int (no first-launch
	// bootstrap; that is owned by ScreenSpaceGI::LoadSettings).
	void ParseSettings(const toml::table& a_root, ScreenSpaceGI::Settings& a_out);

	// Emits canonical visual-look fields into [settings]. Excludes preview_scale / show_preview
	// (matching the pre-existing SSGI SaveSettings asymmetry: those fields live in the struct but
	// were never persisted).
	void EmitSettings(toml::table& a_root, const ScreenSpaceGI::Settings& a_settings);
}
