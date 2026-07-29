#pragma once

#include "Artifact.h"
#include "CanonicalJson.h"

#include <expected>
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

	std::string Sha256Hex(std::string_view a_bytes);

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
}
