#pragma once

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include "Utils/ShaderCache/RevalidationContext.h"
#include "Utils/ShaderCache/ShaderCache.h"
#include "Utils/ShaderCache/SourceCompile.h"
#include "Utils/ShaderCompile.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Test compiles use the runtime shader cache when this names a directory.
namespace cs::shader_test
{
	inline std::optional<std::filesystem::path> CacheRoot()
	{
		static const auto root = [] {
			std::wstring value(MAX_PATH, L'\0');
			const auto length = GetEnvironmentVariableW(L"FO4CS_SHADER_TEST_CACHE", value.data(), static_cast<DWORD>(value.size()));
			if (length > value.size()) {
				value.resize(length);
				GetEnvironmentVariableW(L"FO4CS_SHADER_TEST_CACHE", value.data(), static_cast<DWORD>(value.size()));
			}
			value.resize(length < value.size() ? length : value.size() - 1);
			return value.empty() ? std::optional<std::filesystem::path>{} : std::optional(std::filesystem::path(value));
		}();
		return root;
	}

	// Hits are revalidated against the include trace; only D3DCompile is skipped.
	inline shader_cache::ShaderCacheOutcome Compile(const shader_cache::ShaderRecipe& a_recipe)
	{
		const auto root = CacheRoot();
		if (!root) {
			auto compiled = shader_cache::CompileSourceWithManifest(a_recipe);
			shader_cache::ShaderCacheOutcome outcome;
			outcome.succeeded = compiled.succeeded;
			outcome.bytecode = std::move(compiled.bytecode);
			outcome.error = std::move(compiled.error);
			return outcome;
		}
		static shader_cache::RevalidationContext revalidation;
		return shader_cache::LoadOrCompileShader(a_recipe, { .cacheRoot = *root, .revalidation = &revalidation });
	}

	// cs::util::CompileShaderToBlob semantics, served from the shared test cache.
	inline Microsoft::WRL::ComPtr<ID3DBlob> CompileToBlob(
		const std::filesystem::path& a_file,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_profile,
		std::string* a_outError,
		const std::filesystem::path& a_shaderRoot)
	{
		const auto recipe = util::BuildUtilityShaderRecipe(
			a_file, a_defines, a_profile, "main", a_shaderRoot, util::kUtilityCompileFlags1);
		if (!recipe)
			return util::CompileShaderToBlob(a_file.c_str(), a_defines, a_profile, "main", a_outError, a_shaderRoot);
		const auto outcome = Compile(*recipe);
		if (a_outError)
			*a_outError = outcome.error;
		if (!outcome.succeeded)
			return {};
		return util::MakeBlob(outcome.bytecode, a_outError);
	}
	inline void ReportCache()
	{
		if (!CacheRoot())
			return;
		const auto counters = shader_cache::GetShaderCacheCounters();
		std::printf("Shader cache: %llu hit, %llu absent, %llu stale, %llu rejected, %llu written\n",
			counters.hit, counters.absent, counters.stale, counters.rejected, counters.written);
	}
}
