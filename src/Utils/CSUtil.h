#pragma once

#include <d3d11.h>

#include <string>
#include <utility>
#include <vector>

namespace cs::util
{
	// Compiles HLSL and returns null on failure.
	ID3D11DeviceChild* CompileShader(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program = "main");

	// Returns null before the engine device is ready.
	[[nodiscard]] ID3D11Device* GetD3DDevice();

	// Successful reads delete BOM-aware one-shot markers.
	bool ReadMarker(const char* a_path, char& a_out);
}
