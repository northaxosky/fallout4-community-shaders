#pragma once

#include "Utils/ShaderCache/CacheRecord.h"
#include "Utils/ShaderCache/RevalidationContext.h"
#include "Utils/ShaderCache/ShaderRecipe.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cs::shader_cache
{
	enum class CacheMode : std::uint8_t
	{
		kReadWrite,
		// bypass lookup to heal device-rejected bytecode
		kRecompile
	};

	enum class CompileOrigin : std::uint8_t
	{
		kCacheHit,
		kFreshCompile
	};

	enum class CacheDisposition : std::uint8_t
	{
		kHit,
		kNoCompilerIdentity,
		kAbsent,
		kRejected,
		kStale,
		kBypassed
	};

	const char* DescribeDisposition(CacheDisposition a_disposition) noexcept;

	struct ShaderCacheOptions
	{
		std::filesystem::path cacheRoot;
		// optional batch-wide dependency snapshot
		RevalidationContext* revalidation = nullptr;
	};

	struct ShaderCacheOutcome
	{
		bool                      succeeded = false;
		CompileOrigin             origin      = CompileOrigin::kFreshCompile;
		CacheDisposition          disposition = CacheDisposition::kAbsent;
		std::vector<std::uint8_t> bytecode;
		std::filesystem::path     recordPath;
		bool                      recordWritten = false;
		std::string               error;
		std::string               cacheNote;
	};

	ShaderCacheOutcome LoadOrCompileShader(
		const ShaderRecipe&       a_recipe,
		const ShaderCacheOptions& a_options = {},
		CacheMode                 a_mode    = CacheMode::kReadWrite);
}
