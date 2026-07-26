#pragma once

#include "Render/PixelShaderSwapBroker.h"
#include "Utils/CSSha256.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cs::engine::dxbc_patch
{
	inline constexpr std::size_t kMaximumArtifactBytes = 1024 * 1024;
	inline constexpr std::size_t kMaximumShaderBytes = 16 * 1024 * 1024;

	enum class Target : std::uint16_t
	{
		kDirectional = 1,
		kDirectionalIbl = 2
	};

	enum class CandidateStatus : std::uint8_t
	{
		kPass,
		kFail,
		kUnproven
	};

	struct RuntimeIdentity
	{
		std::string release;
		std::string executableVersion;
		sha256::Sha256Result executableSha256;
		std::uint64_t executableLength = 0;
		std::string engineContractScope;

		auto operator<=>(const RuntimeIdentity&) const = default;
	};

	struct StockIdentity
	{
		std::size_t length = 0;
		sha1::Sha1Result sha1;
		sha256::Sha256Result sha256;
	};

	struct Preimage
	{
		std::size_t offset = 0;
		std::size_t length = 0;
		sha256::Sha256Result sha256;
	};

	struct Insertion
	{
		std::size_t executableDword = 0;
		std::size_t fileOffset = 0;
		std::vector<std::byte> bytes;
		Preimage preimage;
	};

	struct Plan
	{
		std::string recipeId;
		Target target = Target::kDirectional;
		StockIdentity stock;
		std::size_t executableChunkOffset = 0;
		Insertion declaration;
		Insertion code;
		std::array<std::uint8_t, 16> expectedChecksum{};
		std::size_t expectedLength = 0;
		sha1::Sha1Result expectedSha1;
		sha256::Sha256Result expectedSha256;
		sha256::Sha256Result receiptSha256;
	};

	struct CandidateRoute
	{
		ShaderVariantKey variant;
		StockIdentity stock;
		CandidateStatus status = CandidateStatus::kUnproven;
		std::optional<std::size_t> planIndex;
	};

	struct Coverage
	{
		std::size_t candidates = 0;
		std::size_t identities = 0;
		std::size_t pass = 0;
		std::size_t fail = 0;
		std::size_t unproven = 0;
	};

	struct Artifact
	{
		RuntimeIdentity runtime;
		std::string status;
		std::string releaseScope;
		Coverage coverage;
		std::vector<CandidateRoute> routes;
		std::vector<Plan> plans;
	};

	struct ArtifactParseResult
	{
		std::optional<Artifact> artifact;
		std::string error;

		explicit operator bool() const noexcept
		{
			return artifact.has_value();
		}
	};

	ArtifactParseResult ParseArtifact(std::string_view a_bytes);
	ArtifactParseResult LoadPinnedArtifact(
		const std::filesystem::path& a_path,
		std::size_t a_expectedLength,
		const sha256::Sha256Result& a_expectedSha256);

	const CandidateRoute* FindCandidateRoute(
		const Artifact& a_artifact,
		ShaderVariantKeyView a_variant) noexcept;
	bool RuntimeMatches(
		const Artifact& a_artifact,
		const RuntimeIdentity& a_runtime) noexcept;
	bool StockMatches(
		const StockIdentity& a_expected,
		std::size_t a_length,
		const sha1::Sha1Result& a_sha1,
		const sha256::Sha256Result& a_sha256) noexcept;

	enum class PatchBuildStatus : std::uint8_t
	{
		kSuccess,
		kStockMismatch,
		kPreimageMismatch,
		kMalformedDxbc,
		kOutputMismatch,
		kAllocationFailure
	};

	struct PatchBuildResult
	{
		PatchBuildStatus status = PatchBuildStatus::kMalformedDxbc;
		std::vector<std::byte> bytecode;
	};

	std::optional<std::array<std::uint8_t, 16>> ComputeDxbcChecksum(
		std::span<const std::byte> a_bytecode) noexcept;
	bool RewriteDxbcChecksum(std::span<std::byte> a_bytecode) noexcept;
	bool ValidateDxbcChecksum(
		std::span<const std::byte> a_bytecode) noexcept;
	PatchBuildResult BuildPatchedDxbc(
		std::span<const std::byte> a_stockBytecode,
		const Plan& a_plan) noexcept;
}
