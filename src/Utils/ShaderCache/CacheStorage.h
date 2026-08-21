#pragma once

#include "Utils/CSSha256.h"
#include "Utils/ShaderCache/ShaderRecipe.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cs::shader_cache
{
	inline constexpr std::uint64_t kMaxSourceBytes = 64ull * 1024ull * 1024ull;

	enum class FileReadStatus : std::uint8_t
	{
		kOk,
		kMissing,
		kTooLarge,
		kReadFailed
	};

	// one reader keeps probe replay byte-exact
	FileReadStatus ReadFileBytes(
		const std::filesystem::path& a_path,
		std::uint64_t                a_maxBytes,
		std::vector<std::uint8_t>&   a_bytes) noexcept;

	std::filesystem::path DefaultCacheRoot();

	std::filesystem::path BuildRecordPath(
		const std::filesystem::path& a_cacheRoot,
		ShaderCacheStage             a_stage,
		const sha256::Sha256Result&  a_logicalDigest);

	bool WriteRecordAtomically(
		const std::filesystem::path&       a_path,
		std::span<const std::uint8_t>      a_bytes,
		std::string&                       a_error) noexcept;
}
