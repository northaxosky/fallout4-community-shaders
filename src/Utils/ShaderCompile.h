#pragma once

#include "Utils/ShaderDefines.h"
#include "Utils/ShaderInclude.h"

#include <d3dcommon.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cs::util
{
	std::vector<std::pair<const char*, const char*>> UtilityShaderDefines(
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType);

	Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderToBlob(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		std::string* a_outError = nullptr,
		const std::filesystem::path& a_shaderRoot = kDefaultShaderRoot);

	Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderToBlob(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		std::uint32_t a_flags,
		std::string* a_outError = nullptr,
		const std::filesystem::path& a_shaderRoot = kDefaultShaderRoot);
}
