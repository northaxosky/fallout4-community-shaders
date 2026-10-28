#pragma once

#include "Utils/CSSha256.h"
#include "Utils/ShaderCache/CompilerIdentity.h"
#include "Utils/ShaderCache/ShaderRecipe.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cs::shader_cache
{
	inline constexpr std::uint64_t kMaxSourceBytes = 64ull * 1024ull * 1024ull;
	inline constexpr wchar_t kIdentityFileName[] = L"identity.txt";

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
		std::uint64_t a_maxBytes,
		std::vector<std::uint8_t>& a_bytes) noexcept;

	std::filesystem::path DefaultCacheRoot();
	std::uintmax_t ClearCacheRecords(
		const std::filesystem::path& a_cacheRoot,
		std::error_code& a_error);

	struct CacheTrimResult
	{
		std::uint64_t records = 0;
		std::uint64_t bytes = 0;
		std::uint64_t trimmed = 0;
	};

	CacheTrimResult TrimCacheRecords(
		const std::filesystem::path& a_cacheRoot,
		std::uint64_t a_maxBytes) noexcept;

	struct CacheIdentitySyncResult
	{
		bool firstRun = false;
		bool reset = false;
		std::string resetMessage;
		std::string error;
	};

	CacheIdentitySyncResult SynchronizeCacheIdentity(
		const std::filesystem::path& a_cacheRoot,
		const CompilerIdentity& a_identity,
		std::uint32_t a_recordSchemaVersion) noexcept;

	std::filesystem::path BuildRecordPath(
		const std::filesystem::path& a_cacheRoot,
		ShaderCacheStage a_stage,
		const sha256::Sha256Result& a_logicalDigest);

	enum class RecordWriteStatus : std::uint8_t
	{
		kWritten,
		kConcurrentPublication,
		kFailed
	};

	RecordWriteStatus WriteRecord(
		const std::filesystem::path& a_path,
		std::span<const std::uint8_t> a_bytes,
		std::string& a_error) noexcept;
}
