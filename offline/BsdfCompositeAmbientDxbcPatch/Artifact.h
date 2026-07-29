#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Host-only consumer for the producer-published strict-v2 Composite ambient DXBC artifact.
// Nothing here may enter FO4CommunityShaders.dll: there is no runtime type, resolver, or binding.
namespace fo4cs::offline
{
	enum class LoadFailureCode
	{
		kFileUnreadable,
		kLengthMismatch,
		kDigestMismatch,
		kNotCanonical,
		kSchemaViolation,
		kCommitmentMismatch,
		kContractPinMismatch,
		kResourceExhausted,
		kHashUnavailable
	};

	struct LoadFailure
	{
		LoadFailureCode code = LoadFailureCode::kFileUnreadable;
		std::string detail;
	};

	struct OccurrenceRecord
	{
		std::int64_t fxpOrdinal = 0;
		std::string fxpKey;
		std::int64_t dxbcOffset = 0;
		std::int64_t markerOffset = 0;
		std::int64_t byteLength = 0;
		std::string stockSha1;
		std::string stockSha256;
		std::string target;
		std::string receiptSha256;
	};

	struct IdentityRecord
	{
		std::string stockSha256;
		std::string stockSha1;
		std::string target;
		std::int64_t stockLength = 0;
		std::size_t occurrences = 0;
		std::string declarationCommitmentSha256;
		// Verdicts follow the artifact participant-set order.
		std::vector<std::string> verdicts;
	};

	struct PlanRecord
	{
		std::string planId;
		std::string recipeId;
		std::string target;
		std::string stockSha256;
		std::vector<std::string> participants;
		std::string proofStatus;
		std::string mechanicalEvidenceClass;
		std::int64_t stockLength = 0;
		std::int64_t stockDclTemps = 0;
		std::int64_t patchedDclTemps = 0;
		std::string patchedSha256;
		std::string planReceiptSha256;
		std::string staticProofReceiptSha256;
		std::string nativeProofReceiptSha256;
		std::size_t edits = 0;
		std::size_t scratchComponents = 0;
		std::size_t addedResourceClaims = 0;
	};

	struct ArtifactModel
	{
		std::string schema;
		std::int64_t schemaVersion = 0;
		std::string release;
		std::string engineContractScope;
		std::string executableSha256;
		std::string lookupRva;
		std::string archiveMember;
		std::string archiveMemberSha256;
		std::string subclass;
		std::string stage;
		std::string profile;
		std::string keyDomain;
		std::vector<std::string> participantSetOrder;
		std::vector<OccurrenceRecord> occurrences;
		std::vector<IdentityRecord> identities;
		std::vector<PlanRecord> plans;
	};

	// Artifact-carried facts only. None of these is a runtime observation.
	struct OfflineInventory
	{
		std::uint64_t fileLength = 0;
		std::string fileSha256;
		std::size_t occurrences = 0;
		std::size_t stockIdentities = 0;
		std::size_t patchPlans = 0;
		std::size_t passPlans = 0;
		std::size_t occurrenceOutcomes = 0;
		std::size_t blobOutcomes = 0;
		std::size_t nativeChecks = 0;
		std::size_t nativeMutants = 0;
		std::size_t staticMutations = 0;
		std::size_t staticGates = 0;
		std::size_t artifactMutations = 0;
		std::size_t normalizerRows = 0;
		bool runtimeAdmissible = false;
		bool routeJoinRequired = false;
		std::string resolver;
		std::string suppression;
		std::size_t runtimeObservations = 0;
		std::size_t joinReceipts = 0;
		std::size_t routesAdmitted = 0;
		std::size_t routesExclusive = 0;
		bool ownershipPresent = false;
		std::string keyDomain;
		// Opaque provenance: comparable, never rederivable without the unshipped contracts file.
		std::string contractsSha256;
	};

	struct ReportRow
	{
		std::string key;
		std::string value;
	};

	struct OfflineSnapshot
	{
		ArtifactModel model;
		OfflineInventory inventory;
		std::vector<ReportRow> report;
	};

	// One call, one result: a fresh immutable snapshot or one explicit failure. Never a cache.
	std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadPinnedSnapshot(
		const std::filesystem::path& a_path);

	std::string_view DescribeFailure(LoadFailureCode a_code) noexcept;
}
