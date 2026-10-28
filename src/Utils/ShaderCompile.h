#pragma once

#include "Utils/ShaderCache/ShaderRecipe.h"
#include "Utils/ShaderDefines.h"
#include "Utils/ShaderInclude.h"

#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cs::util
{
	inline constexpr std::uint32_t kUtilityCompileFlags1 = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

	// Empty when the profile has no cache stage (gs, hs, ds).
	std::optional<shader_cache::ShaderRecipe> BuildUtilityShaderRecipe(
		const std::filesystem::path& a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		const std::filesystem::path& a_shaderRoot = kDefaultShaderRoot,
		std::uint32_t a_flags1 = shader_cache::kCachedOptimizedFlags1);

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
