#pragma once

#include "Utils/CSSha256.h"
#include "Utils/ShaderCache/DependencyTrace.h"
#include "Utils/ShaderCache/ShaderRecipe.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cs::shader_cache
{
	inline constexpr std::array<std::uint8_t, 8> kRecordMagic{
		'F', 'O', '4', 'C', 'S', 'H', 'C', 0x1A
	};
	inline constexpr std::uint32_t kRecordSchemaVersion = 1;

	inline constexpr std::uint64_t kMaxPayloadBytes    = 64ull * 1024ull * 1024ull;
	inline constexpr std::uint64_t kMaxManifestBytes   = 16ull * 1024ull * 1024ull;
	inline constexpr std::uint32_t kMaxManifestEntries = 4096;
	inline constexpr std::uint32_t kMaxProbesPerEntry  = 64;
	inline constexpr std::uint32_t kMaxLocatorBytes    = 4096;
	inline constexpr std::uint32_t kMaxProfileBytes    = 64;
	inline constexpr std::uint64_t kMaxRecordBytes =
		kMaxPayloadBytes + kMaxManifestBytes + 64ull * 1024ull;

	struct ShaderCacheRecord
	{
		sha256::Sha256Result      logicalDigest{};
		sha256::Sha256Result      recipeDigest{};
		sha256::Sha256Result      dependencyDigest{};
		ShaderCacheStage          stage = ShaderCacheStage::kPixel;
		std::string               profile;
		DependencyManifest        manifest;
		std::vector<std::uint8_t> payload;
	};

	enum class RecordStatus : std::uint8_t
	{
		kOk,
		kBadMagic,
		kUnsupportedVersion,
		kTruncated,
		kLimitExceeded,
		kUnknownStage,
		kMalformedManifest,
		kManifestDigestMismatch,
		kPayloadDigestMismatch,
		kTrailingBytes
	};

	const char* DescribeRecordStatus(RecordStatus a_status) noexcept;

	// canonical encoding hashed as the dependency digest
	bool SerializeDependencyManifest(
		const DependencyManifest&  a_manifest,
		std::vector<std::uint8_t>& a_bytes);
	RecordStatus ParseDependencyManifest(
		std::span<const std::uint8_t> a_bytes,
		DependencyManifest&           a_manifest);

	bool SerializeShaderCacheRecord(
		const ShaderCacheRecord&   a_record,
		std::vector<std::uint8_t>& a_bytes);
	RecordStatus ParseShaderCacheRecord(
		std::span<const std::uint8_t> a_bytes,
		ShaderCacheRecord&            a_record);
}
