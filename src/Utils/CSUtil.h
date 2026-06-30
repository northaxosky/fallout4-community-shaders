#pragma once

#include <d3d11.h>

#include <string>
#include <utility>
#include <vector>

namespace cs::util
{
	// Compile a single HLSL shader from disk. Returns nullptr on failure (errors logged).
	// a_defines: list of (name, value) macro definitions; can be empty.
	// a_programType: "ps_5_0" / "vs_5_0" / "cs_5_0" etc.
	// a_program: entry-point name.
	ID3D11DeviceChild* CompileShader(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program = "main");

	// Engine D3D11 device accessor. Returns nullptr if rendererData/device are not yet initialized.
	[[nodiscard]] ID3D11Device* GetD3DDevice();

	// Read a single byte from a marker file. Returns true iff the file existed and the byte was read.
	// Skips a leading UTF-8 BOM (PowerShell `Out-File -Encoding utf8` writes one).
	// On success the marker is deleted: markers are smoke-harness one-shots and lingering files
	// silently override the next run (and survive crashes between sessions).
	bool ReadMarker(const char* a_path, char& a_out);
}
