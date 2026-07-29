#include "Artifact.h"

#include "ArtifactInternal.h"
#include "CanonicalJson.h"
#include "ContractPins.h"
#include "Sha256.h"

#include <fstream>
#include <new>
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
			bytes.resize(static_cast<std::size_t>(pins::kArtifactLength));
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
			std::string a_bytes,
			std::string a_digest,
			internal::LoadFaultPoint a_fault)
		{
			const auto length = static_cast<std::uint64_t>(a_bytes.size());
			if (a_fault == internal::LoadFaultPoint::kBeforeCanonicalParse)
				throw std::bad_alloc();
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
				validated.inventory.fileSha256 = std::move(a_digest);
				snapshot.report = internal::BuildReport(validated);
				snapshot.model = std::move(validated.model);
				snapshot.inventory = std::move(validated.inventory);
			} catch (const internal::ValidationError& error) {
				return std::unexpected(LoadFailure{ error.code, error.detail });
			}
			if (a_fault == internal::LoadFaultPoint::kBeforeSnapshotAllocation)
				throw std::bad_alloc();
			return std::make_shared<const OfflineSnapshot>(std::move(snapshot));
		}

		hash::Sha256FaultPoint HashFaultFor(internal::LoadFaultPoint a_fault) noexcept
		{
			switch (a_fault) {
			case internal::LoadFaultPoint::kArtifactHashObjectAllocation:
				return hash::Sha256FaultPoint::kHashObjectAllocation;
			case internal::LoadFaultPoint::kArtifactHashCryptoFailure:
				return hash::Sha256FaultPoint::kCryptoFailure;
			case internal::LoadFaultPoint::kNone:
			case internal::LoadFaultPoint::kBeforeCanonicalParse:
			case internal::LoadFaultPoint::kBeforeSnapshotAllocation:
				break;
			}
			return hash::Sha256FaultPoint::kNone;
		}

		// Every allocating stage of the pinned load: bounded read, digest, parse, validate, publish.
		std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> RunPinnedPipeline(
			const std::filesystem::path& a_path,
			internal::LoadFaultPoint a_fault)
		{
			// The bounded read enforces the length pin; the digest pin runs before any decode.
			auto bytes = ReadPinnedBytes(a_path);
			if (!bytes)
				return std::unexpected(bytes.error());
			// The artifact bytes are hashed exactly once: the pin and the inventory share it.
			auto digest = hash::Sha256Hex(*bytes, HashFaultFor(a_fault));
			if (!digest) {
				// An unavailable provider is never a digest verdict, so no comparison happens.
				return std::unexpected(LoadFailure{ LoadFailureCode::kHashUnavailable, {} });
			}
			if (*digest != pins::kArtifactSha256) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kDigestMismatch,
					"artifact digest is not the pinned publication digest" });
			}
			return BuildSnapshot(std::move(*bytes), std::move(*digest), a_fault);
		}

		std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> GuardedPinnedLoad(
			const std::filesystem::path& a_path,
			internal::LoadFaultPoint a_fault)
		{
			try {
				return RunPinnedPipeline(a_path, a_fault);
			} catch (const internal::HashSignal&) {
				// Keep the failure payload allocation-free; DescribeFailure supplies stable text.
				return std::unexpected(LoadFailure{
					LoadFailureCode::kHashUnavailable,
					{} });
			} catch (const std::bad_alloc&) {
				return std::unexpected(LoadFailure{
					LoadFailureCode::kResourceExhausted,
					{} });
			}
		}
	}

	std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadPinnedSnapshot(
		const std::filesystem::path& a_path)
	{
		return GuardedPinnedLoad(a_path, internal::LoadFaultPoint::kNone);
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
		case LoadFailureCode::kResourceExhausted:
			return "resource-exhausted";
		case LoadFailureCode::kHashUnavailable:
			return "hash-unavailable";
		}
		return "unknown";
	}

	namespace internal
	{
		void Reject(LoadFailureCode a_code, std::string a_detail)
		{
			throw ValidationError{ a_code, std::move(a_detail) };
		}

		std::string RequireSha256Hex(std::string_view a_bytes)
		{
			auto digest = hash::Sha256Hex(a_bytes);
			if (!digest)
				throw HashSignal{ digest.error().stage, digest.error().status };
			return std::move(*digest);
		}

		std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadUnpinnedBytes(
			std::string a_bytes)
		{
			auto digest = hash::Sha256Hex(a_bytes);
			if (!digest)
				return std::unexpected(LoadFailure{ LoadFailureCode::kHashUnavailable, {} });
			return BuildSnapshot(std::move(a_bytes), std::move(*digest), LoadFaultPoint::kNone);
		}

		std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadPinnedWithFault(
			const std::filesystem::path& a_path,
			LoadFaultPoint a_fault)
		{
			return GuardedPinnedLoad(a_path, a_fault);
		}

		std::expected<std::shared_ptr<const OfflineSnapshot>, LoadFailure> LoadPinnedWithFaultUnguarded(
			const std::filesystem::path& a_path,
			LoadFaultPoint a_fault)
		{
			return RunPinnedPipeline(a_path, a_fault);
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
