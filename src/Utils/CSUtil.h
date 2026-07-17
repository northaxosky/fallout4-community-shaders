#pragma once

#include <d3d11.h>

#include <string>
#include <utility>
#include <vector>

namespace cs::util
{
	// Compile one HLSL file; a_defines are macros, a_programType is "ps_5_0"/"vs_5_0"/"cs_5_0", and a_program is the entry point; log and return nullptr on failure.
	ID3D11DeviceChild* CompileShader(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program = "main");

	// Engine D3D11 device accessor. Returns nullptr if rendererData/device are not yet initialized.
	[[nodiscard]] ID3D11Device* GetD3DDevice();

	// Reads one marker byte, skipping UTF-8 BOM; successful reads delete smoke one-shots to avoid stale overrides.
	bool ReadMarker(const char* a_path, char& a_out);
}
