#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include "Utils/ShaderCompile.h"
#include "Render/ShaderInjectionDefines.h"
#include "Utils/ShaderCache/SourceCompile.h"

#include <algorithm>
#include <cstring>

namespace cs::util
{
	namespace
	{
		shader_cache::ShaderRecipe MakeUtilityShaderRecipe(
			const std::filesystem::path& a_filePath,
			const std::vector<std::pair<const char*, const char*>>& a_defines,
			const char* a_programType,
			const char* a_program,
			const std::filesystem::path& a_shaderRoot,
			std::uint32_t a_flags1)
		{
			shader_cache::ShaderRecipe recipe;
			recipe.source = a_filePath;
			recipe.includeRoots.push_back(a_shaderRoot);
			recipe.defines.reserve(a_defines.size() + 1);
			// Runtime shaders always read the substrate, as injected ones do.
			if (std::ranges::none_of(a_defines, [](const auto& a_define) { return std::strcmp(a_define.first, cs::engine::shader_injection_defines::kSubstrate) == 0; }))
				recipe.defines.emplace_back(cs::engine::shader_injection_defines::kSubstrate, "1");
			for (const auto& [name, value] : a_defines)
				recipe.defines.emplace_back(name, value);
			recipe.profile = a_programType ? a_programType : "";
			AppendStandardShaderDefines(recipe.defines, recipe.profile);
			recipe.entryPoint = a_program ? a_program : "";
			recipe.flags1 = a_flags1;
			shader_cache::ApplyDebugCompile(recipe);
			return recipe;
		}
	}

	std::optional<shader_cache::ShaderRecipe> BuildUtilityShaderRecipe(
		const std::filesystem::path& a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		const std::filesystem::path& a_shaderRoot,
		std::uint32_t a_flags1)
	{
		auto recipe = MakeUtilityShaderRecipe(
			a_filePath, a_defines, a_programType, a_program, a_shaderRoot, a_flags1);
		if (!shader_cache::TryGetShaderCacheStage(recipe.profile, recipe.stage))
			return std::nullopt;
		return recipe;
	}

	Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderToBlob(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		std::string* a_outError,
		const std::filesystem::path& a_shaderRoot)
	{
		return CompileShaderToBlob(
			a_filePath, a_defines, a_programType, a_program,
			kUtilityCompileFlags1, a_outError, a_shaderRoot);
	}

	Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderToBlob(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		std::uint32_t a_flags,
		std::string* a_outError,
		const std::filesystem::path& a_shaderRoot)
	{
		if (a_outError)
			a_outError->clear();
		const auto recipe = MakeUtilityShaderRecipe(
			a_filePath, a_defines, a_programType, a_program, a_shaderRoot, a_flags);
		auto outcome = shader_cache::CompileSourceWithManifest(recipe);
		if (a_outError)
			*a_outError = outcome.error;
		if (!outcome.succeeded)
			return {};
		return MakeBlob(outcome.bytecode, a_outError);
	}

	Microsoft::WRL::ComPtr<ID3DBlob> MakeBlob(std::span<const std::uint8_t> a_bytecode, std::string* a_outError)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> blob;
		if (FAILED(D3DCreateBlob(a_bytecode.size(), blob.GetAddressOf()))) {
			if (a_outError)
				*a_outError = "Could not allocate shader bytecode blob";
			return {};
		}
		std::memcpy(blob->GetBufferPointer(), a_bytecode.data(), a_bytecode.size());
		return blob;
	}
}
