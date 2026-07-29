#include "Artifact.h"

#include "ArtifactInternal.h"
#include "CanonicalJson.h"
#include "ContractPins.h"

#include "Utils/CSSha256.h"

#include <fstream>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace fo4cs::offline
{
	namespace
	{
		// Reads at most the pinned length plus one overflow probe: an oversized or endless
		// stream can never allocate or block its way past the length gate.
		std::expected<std::string, LoadFailure> ReadPinnedBytes(const std::filesystem::path& a_path)
		{
			std::ifstream stream(a_path, std::ios::binary);
			if (!stream) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kFileUnreadable,
					"artifact cannot be opened" });
			}
			std::string bytes;
			try {
				bytes.resize(static_cast<std::size_t>(pins::kArtifactLength));
			} catch (const std::bad_alloc&) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kFileUnreadable,
					"artifact buffer cannot be allocated" });
			} catch (const std::length_error&) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kFileUnreadable,
					"artifact buffer cannot be allocated" });
			}
			stream.read(bytes.data(), static_cast<std::streamsize>(pins::kArtifactLength));
			if (stream.bad()) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kFileUnreadable,
					"artifact cannot be read" });
			}
			if (static_cast<std::uint64_t>(stream.gcount()) != pins::kArtifactLength) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kLengthMismatch,
					"artifact is shorter than the pinned publication length" });
			}
			stream.clear();
			char overflow = 0;
			stream.read(&overflow, 1);
			if (stream.bad()) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kFileUnreadable,
					"artifact cannot be read" });
			}
			if (stream.gcount() != 0) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kLengthMismatch,
					"artifact is longer than the pinned publication length" });
			}
			return bytes;
		}

		std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> BuildSnapshot(
			std::string a_bytes)
		{
			const auto length = static_cast<std::uint64_t>(a_bytes.size());
			const auto digest = internal::Sha256Hex(a_bytes);
			auto document = canonical::ParseCanonical(std::move(a_bytes));
			if (!document) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kNotCanonical,
					document.error() });
			}
			OfflineSnapshot snapshot;
			try {
				auto validated = internal::ValidateArtifact(document->Root());
				validated.inventory.fileLength = length;
				validated.inventory.fileSha256 = digest;
				snapshot.report = internal::BuildReport(validated);
				snapshot.model = std::move(validated.model);
				snapshot.inventory = std::move(validated.inventory);
			} catch (const internal::ValidationError& error) {
				return std::unexpected(LoadFailure{ error.code, error.detail });
			}
			return std::make_shared<const OfflineSnapshot>(std::move(snapshot));
		}
	}

	std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadPinnedSnapshot(
		const std::filesystem::path& a_path)
	{
		// The bounded read enforces the length pin; the digest pin runs before any decode.
		auto bytes = ReadPinnedBytes(a_path);
		if (!bytes)
			return std::unexpected(bytes.error());
		if (internal::Sha256Hex(*bytes) != pins::kArtifactSha256) {
			return std::unexpected(LoadFailure{
				LoadFailureCode::kDigestMismatch,
				"artifact digest is not the pinned publication digest" });
		}
		return BuildSnapshot(std::move(*bytes));
	}

	std::string_view DescribeFailure(LoadFailureCode a_code) noexcept
	{
		switch (a_code) {
		case LoadFailureCode::kFileUnreadable:
			return "file-unreadable";
		case LoadFailureCode::kLengthMismatch:
			return "length-mismatch";
		case LoadFailureCode::kDigestMismatch:
			return "digest-mismatch";
		case LoadFailureCode::kNotCanonical:
			return "not-canonical";
		case LoadFailureCode::kSchemaViolation:
			return "schema-violation";
		case LoadFailureCode::kCommitmentMismatch:
			return "commitment-mismatch";
		case LoadFailureCode::kContractPinMismatch:
			return "contract-pin-mismatch";
		}
		return "unknown";
	}

	namespace internal
	{
		void Reject(LoadFailureCode a_code, std::string a_detail)
		{
			throw ValidationError{ a_code, std::move(a_detail) };
		}

		std::string Sha256Hex(std::string_view a_bytes)
		{
			cs::sha256::Sha256InitOnce();
			return cs::sha256::Sha256ToHex(
				cs::sha256::Sha256Compute(a_bytes.data(), a_bytes.size()));
		}

		std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadUnpinnedBytes(
			std::string a_bytes)
		{
			return BuildSnapshot(std::move(a_bytes));
		}

		std::vector<ReportRow> BuildReport(const ValidatedArtifact& a_validated)
		{
			const auto& inventory = a_validated.inventory;
			const auto& model = a_validated.model;
			const auto number = [](std::size_t a_value) { return std::to_string(a_value); };
			std::vector<ReportRow> rows{
				{ "schema", model.schema },
				{ "schema_version", std::to_string(model.schemaVersion) },
				{ "release", model.release },
				{ "engine_contract_scope", model.engineContractScope },
				{ "archive_member", model.archiveMember },
				{ "subclass", model.subclass },
				{ "stage", model.stage },
				{ "profile", model.profile },
				{ "key_domain", inventory.keyDomain },
				{ "occurrences", number(inventory.occurrences) },
				{ "stock_identities", number(inventory.stockIdentities) },
				{ "occurrence_outcomes", number(inventory.occurrenceOutcomes) },
				{ "blob_outcomes", number(inventory.blobOutcomes) },
				{ "patch_plans", number(inventory.patchPlans) },
				{ "pass_patch_plans", number(inventory.passPlans) },
				{ "static_gates", number(inventory.staticGates) },
				{ "static_mutations", number(inventory.staticMutations) },
				{ "artifact_validation_mutations", number(inventory.artifactMutations) },
				{ "native_checks", number(inventory.nativeChecks) },
				{ "native_mutants", number(inventory.nativeMutants) },
				{ "normalizer_diagnostic_rows", number(inventory.normalizerRows) },
				{ "runtime_admissible", inventory.runtimeAdmissible ? "true" : "false" },
				{ "route_join_required", inventory.routeJoinRequired ? "true" : "false" },
				{ "resolver", inventory.resolver },
				{ "suppression", inventory.suppression },
				{ "runtime_observations", number(inventory.runtimeObservations) },
				{ "join_receipts", number(inventory.joinReceipts) },
				{ "runtime_routes_admitted", number(inventory.routesAdmitted) },
				{ "runtime_routes_exclusive", number(inventory.routesExclusive) },
				{ "ownership", inventory.ownershipPresent ? "present" : "absent" },
				{ "contracts_sha256", inventory.contractsSha256 },
				{ "artifact_length", std::to_string(inventory.fileLength) },
				{ "artifact_sha256", inventory.fileSha256 }
			};
			for (const auto& plan : model.plans) {
				rows.push_back(ReportRow{
					"plan:" + plan.planId,
					plan.proofStatus + " " + plan.mechanicalEvidenceClass + " " + plan.patchedSha256 });
			}
			return rows;
		}
	}
}
