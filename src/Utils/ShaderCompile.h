#pragma once

#include <d3dcommon.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cs::util
{
	Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderToBlob(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		std::string* a_outError = nullptr);

	Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderToBlob(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		std::uint32_t a_flags,
		std::string* a_outError = nullptr);
}
