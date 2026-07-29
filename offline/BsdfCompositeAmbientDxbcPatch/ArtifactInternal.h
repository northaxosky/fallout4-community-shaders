#pragma once

#include "Artifact.h"
#include "CanonicalJson.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fo4cs::offline::internal
{
	struct ValidationError
	{
		LoadFailureCode code = LoadFailureCode::kSchemaViolation;
		std::string detail;
	};

	[[noreturn]] void Reject(LoadFailureCode a_code, std::string a_detail);

	// Non-allocating signal: carries a host hash status through semantic code to the boundary.
	struct HashSignal
	{
		const char* stage = "";
		std::int32_t status = 0;
	};

	// Throws HashSignal when the host hash provider fails; only GuardedPinnedLoad catches it.
	std::string RequireSha256Hex(std::string_view a_bytes);

	struct ValidatedArtifact
	{
		ArtifactModel model;
		OfflineInventory inventory;
	};

	// Throws ValidationError on the first violated reviewed fact; never returns a partial model.
	ValidatedArtifact ValidateArtifact(const canonical::Value& a_root);

	std::vector<ReportRow> BuildReport(const ValidatedArtifact& a_validated);

	// Test-only seam so canonical and semantic attacks stay reachable without the whole-file pin.
	std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadUnpinnedBytes(
		std::string a_bytes);

	// Deterministic local fault points; every production call passes kNone.
	enum class LoadFaultPoint
	{
		kNone,
		kBeforeCanonicalParse,
		kBeforeSnapshotAllocation,
		kArtifactHashObjectAllocation,
		kArtifactHashCryptoFailure
	};

	// Test-only seam: the exact production pinned pipeline behind the same fail-closed boundary.
	std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadPinnedWithFault(
		const std::filesystem::path& a_path,
		LoadFaultPoint a_fault);

	// Unguarded on purpose: proves each fault point really throws without the public boundary.
	std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadPinnedWithFaultUnguarded(
		const std::filesystem::path& a_path,
		LoadFaultPoint a_fault);
}
