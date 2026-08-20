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
		// Skips the lookup and republishes; used to heal a record whose bytecode the device rejected.
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
		// Non-owning: when set, the whole batch shares one snapshot of the dependency tree.
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

	// Returns validated cached bytecode when every dependency still matches, otherwise compiles.
	ShaderCacheOutcome LoadOrCompileShader(
		const ShaderRecipe&       a_recipe,
		const ShaderCacheOptions& a_options = {},
		CacheMode                 a_mode    = CacheMode::kReadWrite);
}
