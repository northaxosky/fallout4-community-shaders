#pragma once

#include "Registry.h"

#include <d3d11.h>

namespace cs::features::replacement
{
	// Compiles HLSL at e.hlsl_abs_path with profile/entry/defines from the manifest,
	// populates e.compile_ok / e.compile_error / e.compiled_ps / e.compiled_sha1_hex.
	// Logs a single line per entry. Safe to call only post-OnD3D11Ready.
	bool CompileEntry(ID3D11Device* device, ReplacementEntry& e);
}
