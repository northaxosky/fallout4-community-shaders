#include "Artifact.h"
#include "ArtifactInternal.h"
#include "CanonicalJson.h"
#include "Sha256.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using fo4cs::offline::DescribeFailure;
	using fo4cs::offline::LoadFailureCode;
	using fo4cs::offline::LoadPinnedSnapshot;
	using fo4cs::offline::canonical::CompactNode;
	using fo4cs::offline::canonical::CompactString;
	using fo4cs::offline::canonical::Document;
	using fo4cs::offline::canonical::Member;
	using fo4cs::offline::canonical::ParseCanonical;
	using fo4cs::offline::canonical::Value;
	using fo4cs::offline::canonical::ValueKind;
	using fo4cs::offline::hash::Sha256FaultPoint;
	using fo4cs::offline::internal::LoadFaultPoint;
	using fo4cs::offline::internal::LoadPinnedWithFault;
	using fo4cs::offline::internal::LoadPinnedWithFaultUnguarded;
	using fo4cs::offline::internal::LoadUnpinnedBytes;
	using fo4cs::offline::internal::RequireSha256Hex;

	constexpr std::uint64_t kArtifactLength = 871524;
	constexpr std::string_view kArtifactSha256 =
		"a721850c943d50568f7a1dbb91d08bae203c69aac253a3e81dd7dface32037e3";
	constexpr std::string_view kSssArtifactSha256 =
		"eaa508727cd08ed20b9d3c5db823eebe28e23b78f948e179dab84ae9217d8483";

	struct Failure : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw Failure(std::string(a_message));
	}

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		Check(static_cast<bool>(stream), "fixture is unreadable: " + a_path.string());
		return std::string(
			(std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	}

	// Independent canonical writer: the mutation harness must not reuse a library writer.
	void WriteCanonical(const Value& a_value, std::size_t a_depth, std::string& a_out)
	{
		const auto indent = [&a_out](std::size_t a_width) { a_out.append(a_width * 2, ' '); };
		switch (a_value.kind) {
		case ValueKind::kObject:
			if (a_value.members.empty()) {
				a_out += "{}";
				return;
			}
			a_out.push_back('{');
			for (std::size_t index = 0; index < a_value.members.size(); ++index) {
				if (index != 0)
					a_out.push_back(',');
				a_out.push_back('\n');
				indent(a_depth + 1);
				a_out += CompactString(a_value.members[index].key);
				a_out += ": ";
				WriteCanonical(a_value.members[index].value, a_depth + 1, a_out);
			}
			a_out.push_back('\n');
			indent(a_depth);
			a_out.push_back('}');
			return;
		case ValueKind::kArray:
			if (a_value.items.empty()) {
				a_out += "[]";
				return;
			}
			a_out.push_back('[');
			for (std::size_t index = 0; index < a_value.items.size(); ++index) {
				if (index != 0)
					a_out.push_back(',');
				a_out.push_back('\n');
				indent(a_depth + 1);
				WriteCanonical(a_value.items[index], a_depth + 1, a_out);
			}
			a_out.push_back('\n');
			indent(a_depth);
			a_out.push_back(']');
			return;
		default:
			a_out += a_value.lexeme;
			return;
		}
	}

	std::string Serialize(const Value& a_root)
	{
		std::string out;
		WriteCanonical(a_root, 0, out);
		out.push_back('\n');
		return out;
	}

	// Stable storage for the lexemes the mutation harness invents.
	std::deque<std::string> g_arena;

	Value MakeString(std::string a_text)
	{
		g_arena.push_back(CompactString(a_text));
		Value value;
		value.kind = ValueKind::kString;
		value.lexeme = g_arena.back();
		value.text = std::move(a_text);
		return value;
	}

	Value MakeInteger(std::int64_t a_number)
	{
		g_arena.push_back(std::to_string(a_number));
		Value value;
		value.kind = ValueKind::kInteger;
		value.lexeme = g_arena.back();
		value.integer = a_number;
		return value;
	}

	Value MakeBoolean(bool a_value)
	{
		g_arena.push_back(a_value ? "true" : "false");
		Value value;
		value.kind = ValueKind::kBoolean;
		value.lexeme = g_arena.back();
		value.boolean = a_value;
		return value;
	}

	Value MakeNull()
	{
		g_arena.push_back("null");
		Value value;
		value.kind = ValueKind::kNull;
		value.lexeme = g_arena.back();
		return value;
	}

	Value MakeStringArray(std::vector<std::string> a_items)
	{
		Value value;
		value.kind = ValueKind::kArray;
		for (auto& item : a_items)
			value.items.push_back(MakeString(std::move(item)));
		return value;
	}

	Value& At(Value& a_object, std::string_view a_key)
	{
		for (auto& member : a_object.members) {
			if (member.key == a_key)
				return member.value;
		}
		throw Failure("mutation target is absent: " + std::string(a_key));
	}

	void Set(Value& a_object, std::string_view a_key, Value a_value)
	{
		At(a_object, a_key) = std::move(a_value);
	}

	void Erase(Value& a_object, std::string_view a_key)
	{
		const auto found = std::find_if(
			a_object.members.begin(), a_object.members.end(), [a_key](const Member& a_member) {
				return a_member.key == a_key;
			});
		Check(found != a_object.members.end(), "erase target is absent");
		a_object.members.erase(found);
	}

	std::string Compact(std::string_view a_key, const Value& a_value)
	{
		return std::string(CompactString(a_key)) + ":" + CompactNode(a_value);
	}

	// Rebuilds one native check evidence receipt exactly as the producer does.
	std::string CheckEvidenceReceipt(const Value& a_native, const Value& a_row)
	{
		std::vector<std::string> members{
			Compact("active_pixels", *a_row.Find("active_pixels")),
			Compact("check", *a_row.Find("check")),
			Compact("detail", *a_row.Find("detail")),
			Compact("evidence_class", *a_row.Find("evidence_class")),
			Compact("execution_plan_sha256", *a_native.Find("execution_plan_sha256")),
			Compact("expected", *a_row.Find("expected")),
			Compact("input_receipts", *a_row.Find("input_receipts")),
			Compact("inputs", *a_row.Find("inputs")),
			Compact("kind", *a_row.Find("kind")),
			Compact("passed", *a_row.Find("passed")),
			Compact("run_receipt_sha256", *a_native.Find("run_receipt_sha256"))
		};
		std::string preimage("{");
		for (std::size_t index = 0; index < members.size(); ++index) {
			if (index != 0)
				preimage.push_back(',');
			preimage += members[index];
		}
		preimage.push_back('}');
		return RequireSha256Hex(preimage);
	}

	std::string StaticMutationReceipt(const Value& a_row)
	{
		std::string preimage("{");
		bool first = true;
		for (const auto& member : a_row.members) {
			if (member.key == "evidence_receipt_sha256")
				continue;
			if (!first)
				preimage.push_back(',');
			first = false;
			preimage += Compact(member.key, member.value);
		}
		preimage.push_back('}');
		return RequireSha256Hex(preimage);
	}

	// A forger who also repairs the whole-document claims commitment still faces the reviewed pins.
	std::string ArtifactClaimsCommitment(const Value& a_root)
	{
		std::string preimage("{");
		bool first = true;
		for (const auto& member : a_root.members) {
			if (!first)
				preimage.push_back(',');
			first = false;
			if (member.key != "receipts") {
				preimage += Compact(member.key, member.value);
				continue;
			}
			std::string receipts("{");
			bool firstReceipt = true;
			for (const auto& receipt : member.value.members) {
				if (receipt.key == "artifact_claims_commitment_sha256" ||
					receipt.key == "contracts_sha256")
					continue;
				if (!firstReceipt)
					receipts.push_back(',');
				firstReceipt = false;
				receipts += Compact(receipt.key, receipt.value);
			}
			receipts.push_back('}');
			preimage += CompactString(member.key);
			preimage.push_back(':');
			preimage += receipts;
		}
		preimage.push_back('}');
		return RequireSha256Hex(preimage);
	}

	std::string NativeProofReceipt(const Value& a_native)
	{
		std::string preimage("{");
		preimage += Compact("proof", a_native);
		preimage.push_back(',');
		preimage += CompactString("schema");
		preimage.push_back(':');
		preimage += CompactString("fo4re.bsdf-composite-ambient-native-proof");
		preimage.push_back(',');
		preimage += CompactString("schema_version");
		preimage += ":2";
		preimage.push_back('}');
		return RequireSha256Hex(preimage);
	}

	void ExpectFailure(const Value& a_root, std::string_view a_label, LoadFailureCode a_expected)
	{
		const auto result = LoadUnpinnedBytes(Serialize(a_root));
		Check(!result.has_value(), std::string(a_label) + " was accepted");
		Check(
			result.error().code == a_expected,
			std::string(a_label) + " failed with the wrong code: " + result.error().detail);
	}

	void ExpectTextFailure(
		std::string a_bytes,
		std::string_view a_label,
		LoadFailureCode a_expected,
		std::string_view a_diagnostic = {})
	{
		const auto result = LoadUnpinnedBytes(std::move(a_bytes));
		Check(!result.has_value(), std::string(a_label) + " was accepted");
		Check(
			result.error().code == a_expected,
			std::string(a_label) + " failed with the wrong code: " + result.error().detail);
		Check(
			a_diagnostic.empty() ||
				result.error().detail.find(a_diagnostic) != std::string::npos,
			std::string(a_label) + " did not reach its diagnostic: " + result.error().detail);
	}

	std::string Replace(std::string_view a_bytes, std::string_view a_needle, std::string_view a_value)
	{
		const auto position = a_bytes.find(a_needle);
		Check(
			position != std::string_view::npos,
			"mutation needle is absent: " + std::string(a_needle));
		std::string mutated(a_bytes);
		mutated.replace(position, a_needle.size(), a_value);
		return mutated;
	}

	void TestPinnedLoad(const std::filesystem::path& a_path, const std::string& a_bytes)
	{
		Check(a_bytes.size() == kArtifactLength, "artifact length is not the pinned length");
		Check(RequireSha256Hex(a_bytes) == kArtifactSha256, "artifact digest is not the pinned digest");

		const auto snapshot = LoadPinnedSnapshot(a_path);
		Check(snapshot.has_value(), "the pinned publication was rejected");
		const auto& inventory = (*snapshot)->inventory;
		Check(inventory.fileLength == kArtifactLength, "inventory length drifted");
		Check(inventory.fileSha256 == kArtifactSha256, "inventory digest drifted");
		Check(inventory.occurrences == 180, "inventory occurrence count is wrong");
		Check(inventory.stockIdentities == 78, "inventory identity count is wrong");
		Check(inventory.patchPlans == 6 && inventory.passPlans == 6, "inventory plan count is wrong");
		Check(inventory.occurrenceOutcomes == 720, "inventory occurrence outcomes are wrong");
		Check(inventory.blobOutcomes == 312, "inventory blob outcomes are wrong");
		Check(inventory.nativeChecks == 344, "inventory native check count is wrong");
		Check(inventory.nativeMutants == 12, "inventory native mutant count is wrong");
		Check(inventory.staticMutations == 38, "inventory static mutation count is wrong");
		Check(inventory.staticGates == 24, "inventory static gate count is wrong");
		Check(inventory.artifactMutations == 7, "inventory artifact mutation count is wrong");
		Check(inventory.normalizerRows == 18, "inventory normalizer row count is wrong");

		Check(!inventory.runtimeAdmissible, "the artifact was reported runtime admissible");
		Check(inventory.routeJoinRequired, "the artifact dropped its route join requirement");
		Check(inventory.resolver == "no-match", "resolver is not no-match");
		Check(inventory.suppression == "none", "suppression is not none");
		Check(inventory.runtimeObservations == 0, "runtime observations are not zero");
		Check(inventory.joinReceipts == 0, "join receipts are not zero");
		Check(inventory.routesAdmitted == 0, "admitted routes are not zero");
		Check(inventory.routesExclusive == 0, "exclusive routes are not zero");
		Check(!inventory.ownershipPresent, "ownership is not absent");
		Check(inventory.keyDomain == "archive_fxp_key", "key domain left the archive domain");
		Check(
			inventory.contractsSha256 ==
				"3ea1eadd04e57141b93fe51f6d26d167f83a073c773d23cf3ebcebe6e792d54e",
			"opaque contracts digest drifted");

		const auto& model = (*snapshot)->model;
		Check(model.schema == "fo4re.bsdf-composite-ambient-dxbc-patch-plans", "schema is wrong");
		Check(model.schemaVersion == 2, "schema version is wrong");
		Check(model.release == "fallout4-ae-1.11.221.0", "engine release is wrong");
		Check(model.subclass == "BSDFCompositeShader", "subclass is wrong");
		Check(model.occurrences.size() == 180, "model occurrence count is wrong");
		Check(model.identities.size() == 78, "model identity count is wrong");
		Check(model.plans.size() == 6, "model plan count is wrong");
		Check(
			model.participantSetOrder ==
				std::vector<std::string>{ "stock", "SSGI", "Wetness", "SSGI+Wetness" },
			"participant set order is wrong");
		for (const auto& plan : model.plans)
			Check(plan.proofStatus == "PASS", "a published plan is not PASS");

		// Every allocating stage is fail-closed: the seam runs the exact production pipeline.
		for (const auto fault : { LoadFaultPoint::kBeforeCanonicalParse,
				 LoadFaultPoint::kBeforeSnapshotAllocation,
				 LoadFaultPoint::kArtifactHashObjectAllocation }) {
			bool escaped = false;
			try {
				(void)LoadPinnedWithFaultUnguarded(a_path, fault);
			} catch (const std::bad_alloc&) {
				escaped = true;
			}
			Check(escaped, "an unguarded fault point did not raise a real allocation failure");

			const auto guarded = LoadPinnedWithFault(a_path, fault);
			Check(!guarded.has_value(), "an allocation failure produced a snapshot");
			Check(
				guarded.error().code == LoadFailureCode::kResourceExhausted,
				"an allocation failure did not map to the dedicated failure code");
			Check(
				DescribeFailure(guarded.error().code) == "resource-exhausted",
				"the resource-exhausted description is not stable");
			Check(
				guarded.error().detail.empty(),
				"the resource-exhausted failure allocated diagnostic detail");
		}

		// A crypto failure is an explicit status result from the host helper, never an exception.
		const auto direct = fo4cs::offline::hash::Sha256Hex(a_bytes, Sha256FaultPoint::kCryptoFailure);
		Check(!direct.has_value(), "the faulted host hash returned a digest");
		Check(
			direct.error().stage != nullptr && *direct.error().stage != '\0',
			"the host hash failure carries no stage");
		Check(direct.error().status != 0, "the host hash failure carries no provider status");
		const auto healthy = fo4cs::offline::hash::Sha256Hex(a_bytes);
		Check(healthy.has_value(), "the host hash rejected the publication bytes");
		Check(*healthy == kArtifactSha256, "the host hash does not reproduce the pinned digest");

		// A hash provider failure is never a digest mismatch and never publishes a zero digest.
		const auto unguarded =
			LoadPinnedWithFaultUnguarded(a_path, LoadFaultPoint::kArtifactHashCryptoFailure);
		Check(!unguarded.has_value(), "the unguarded crypto seam produced a snapshot");
		Check(
			unguarded.error().code == LoadFailureCode::kHashUnavailable,
			"the unguarded crypto seam did not return the dedicated failure code");
		Check(
			unguarded.error().detail.empty(),
			"the unguarded crypto seam allocated diagnostic detail");

		const auto unavailable =
			LoadPinnedWithFault(a_path, LoadFaultPoint::kArtifactHashCryptoFailure);
		Check(!unavailable.has_value(), "a hash provider failure produced a snapshot");
		Check(
			unavailable.error().code == LoadFailureCode::kHashUnavailable,
			"a hash provider failure did not map to the dedicated failure code");
		Check(
			unavailable.error().code != LoadFailureCode::kDigestMismatch,
			"a hash provider failure was misclassified as a digest mismatch");
		Check(
			DescribeFailure(unavailable.error().code) == "hash-unavailable",
			"the hash-unavailable description is not stable");
		Check(
			unavailable.error().detail.empty(),
			"the hash-unavailable failure allocated diagnostic detail");

		const auto recovered = LoadPinnedSnapshot(a_path);
		Check(recovered.has_value(), "the loader did not recover after an injected fault");
		Check(
			(*recovered)->inventory.fileSha256 == kArtifactSha256,
			"the recovered inventory digest is not the exact pin");
	}

	void TestWholeBytePin(const std::string& a_bytes)
	{
		const auto directory = std::filesystem::temp_directory_path();
		const auto path = directory / "fo4cs-composite-pin.json";

		const auto write = [&path](const std::string& a_content) {
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
		};
		const auto expect = [&path](LoadFailureCode a_code, std::string_view a_label) {
			const auto result = LoadPinnedSnapshot(path);
			Check(!result.has_value(), std::string(a_label) + " was accepted");
			Check(
				result.error().code == a_code,
				std::string(a_label) + " failed with the wrong code");
		};

		auto truncated = a_bytes;
		truncated.pop_back();
		write(truncated);
		expect(LoadFailureCode::kLengthMismatch, "a truncated publication");

		write(a_bytes + "\n");
		expect(LoadFailureCode::kLengthMismatch, "an oversized publication");

		auto flipped = a_bytes;
		flipped[flipped.size() / 2] = flipped[flipped.size() / 2] == 'a' ? 'b' : 'a';
		write(flipped);
		expect(LoadFailureCode::kDigestMismatch, "a same-length byte flip");

		// A length-correct, UTF-8-invalid mutant proves the pin runs before any decode.
		auto invalidUtf8 = a_bytes;
		invalidUtf8[100] = static_cast<char>(0xFF);
		write(invalidUtf8);
		expect(LoadFailureCode::kDigestMismatch, "an invalid UTF-8 mutant");

		write(a_bytes);
		const auto accepted = LoadPinnedSnapshot(path);
		Check(accepted.has_value(), "the exact copied publication was rejected");
		std::filesystem::remove(path);

		const auto missing = LoadPinnedSnapshot(directory / "fo4cs-composite-absent.json");
		Check(!missing.has_value(), "an absent publication was accepted");
		Check(
			missing.error().code == LoadFailureCode::kFileUnreadable,
			"an absent publication failed with the wrong code");
	}

	// Canonical but non-identical bytes must lose the public loader before any semantic work.
	void TestSemanticByteDrift(const Value& a_root)
	{
		const auto path = std::filesystem::temp_directory_path() / "fo4cs-composite-drift.json";
		const auto write = [&path](const std::string& a_content) {
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
		};

		write(Serialize(a_root));
		Check(
			LoadPinnedSnapshot(path).has_value(),
			"a byte-identical reserialization was rejected");

		auto reordered = a_root;
		std::swap(reordered.members[0], reordered.members[1]);
		write(Serialize(reordered));
		const auto result = LoadPinnedSnapshot(path);
		Check(!result.has_value(), "semantically equal byte drift was accepted");
		Check(
			result.error().code == LoadFailureCode::kDigestMismatch,
			"semantically equal byte drift did not fail the whole-byte pin");
		std::filesystem::remove(path);
	}

	void TestCanonicalRoundTrip(const std::string& a_bytes, const Document& a_document)
	{
		Check(
			Serialize(a_document.Root()) == a_bytes,
			"the parsed tree does not round-trip the publication bytes");
		const auto accepted = LoadUnpinnedBytes(a_bytes);
		Check(accepted.has_value(), "the unpinned seam rejected the exact publication");

		// The three committed double lexemes must survive every compact commitment unchanged.
		const auto& checks =
			*a_document.Root().Find("byte_proof")->Find("native_proof")->Find("checks");
		std::size_t tolerances = 0;
		std::size_t ndotvs = 0;
		std::size_t wetness = 0;
		for (const auto& row : checks.items) {
			const auto compact = CompactNode(*row.Find("expected"));
			if (compact.find("\"tolerance\":2e-05") != std::string::npos)
				++tolerances;
			if (compact.find("\"ndotv\":1.0,") != std::string::npos)
				++ndotvs;
			if (compact.find("\"wetness\":1.0") != std::string::npos)
				++wetness;
			if (compact.find("\"tolerance\":0.00025") != std::string::npos)
				++tolerances;
		}
		Check(tolerances == 6, "the committed tolerance lexemes did not survive the compact writer");
		Check(ndotvs == 2, "the committed ndotv lexemes did not survive the compact writer");
		Check(wetness == 2, "the committed wetness lexemes did not survive the compact writer");
	}

	void TestCanonicalAttacks(const std::string& a_bytes)
	{
		auto invalidUtf8 = a_bytes;
		invalidUtf8[100] = static_cast<char>(0xFF);
		ExpectTextFailure(
			std::move(invalidUtf8), "an invalid UTF-8 byte", LoadFailureCode::kNotCanonical);

		auto carriageReturn = a_bytes;
		carriageReturn[10] = '\r';
		ExpectTextFailure(std::move(carriageReturn), "a CR byte", LoadFailureCode::kNotCanonical);

		std::string crlf;
		for (const char character : a_bytes) {
			if (character == '\n')
				crlf.push_back('\r');
			crlf.push_back(character);
		}
		ExpectTextFailure(std::move(crlf), "CRLF line endings", LoadFailureCode::kNotCanonical);

		auto withoutFinalLf = a_bytes;
		withoutFinalLf.pop_back();
		ExpectTextFailure(
			std::move(withoutFinalLf), "a missing final LF", LoadFailureCode::kNotCanonical);

		ExpectTextFailure(a_bytes + "\n", "a trailing byte", LoadFailureCode::kNotCanonical);

		ExpectTextFailure(
			Replace(a_bytes, "\n  \"archive\": {", "\n    \"archive\": {"),
			"a reindented member",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\n  \"archive\": {", "\n  \"archive\" : {"),
			"a widened key separator",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\n  \"archive\": {", "\n  \"archive\":{"),
			"a narrowed key separator",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"schema_version\": 2", "\"schema_version\": 02"),
			"a leading-zero integer",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"schema_version\": 2", "\"schema_version\": 2e0"),
			"an exponent integer",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"schema_version\": 2", "\"schema_version\": +2"),
			"a signed integer",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"tolerance\": 2e-05", "\"tolerance\": 2e-5"),
			"an alternate double exponent",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"tolerance\": 2e-05", "\"tolerance\": 2E-05"),
			"an upper-case double exponent",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"tolerance\": 0.00025", "\"tolerance\": 2.5e-04"),
			"a numerically equal double spelling",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"ndotv\": 1.0", "\"ndotv\": 1.00"),
			"a padded double spelling",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"ndotv\": 1.0", "\"ndotv\": -0.0"),
			"a negative zero",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"ndotv\": 1.0", "\"ndotv\": NaN"),
			"a NaN literal",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(a_bytes, "\"ndotv\": 1.0", "\"ndotv\": Infinity"),
			"an Infinity literal",
			LoadFailureCode::kNotCanonical);
		// An integer in a double slot stays canonical, so the schema census must reject it.
		ExpectTextFailure(
			Replace(a_bytes, "\"ndotv\": 1.0", "\"ndotv\": 1"),
			"an integer in a double slot",
			LoadFailureCode::kSchemaViolation);
		ExpectTextFailure(
			Replace(a_bytes, "\"scope\": \"AE", "\"scope\": \"\\u0041E"),
			"a unicode escape",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure(
			Replace(
				a_bytes,
				"\"member\": \"shadersfx/shaders011.fxp\"",
				"\"member\": \"shadersfx\\/shaders011.fxp\""),
			"a solidus escape",
			LoadFailureCode::kNotCanonical);
		ExpectTextFailure("[\n  1\n]\n", "a non-object root", LoadFailureCode::kNotCanonical);

		// The duplicate must sit at the exact canonical depth of the root schema_version member.
		ExpectTextFailure(
			Replace(
				a_bytes,
				"\"schema_version\": 2,\n  \"stock_identities\": [",
				"\"schema_version\": 2,\n  \"schema_version\": 2,\n  \"stock_identities\": ["),
			"a duplicate key",
			LoadFailureCode::kNotCanonical,
			"object repeats a key");

		// A raw DEL byte is injected directly, so the rejection cannot depend on CompactString.
		auto delByte = a_bytes;
		const auto scope = delByte.find("\"scope\": \"AE");
		Check(scope != std::string::npos, "the byte proof scope fixture is absent");
		delByte[scope + 11] = static_cast<char>(0x7F);
		ExpectTextFailure(
			std::move(delByte), "a raw DEL byte", LoadFailureCode::kNotCanonical, "DEL byte");
	}

	void TestKeyOrderAttacks(const Value& a_root)
	{
		const auto swapFirstTwo = [](Value& a_object) {
			Check(a_object.members.size() >= 2, "the key order attack needs two members");
			std::swap(a_object.members[0], a_object.members[1]);
		};

		auto rootSwap = a_root;
		swapFirstTwo(rootSwap);
		ExpectFailure(rootSwap, "a reordered root", LoadFailureCode::kNotCanonical);

		auto scopeSwap = a_root;
		swapFirstTwo(At(scopeSwap, "engine_scope"));
		ExpectFailure(scopeSwap, "a reordered engine scope", LoadFailureCode::kNotCanonical);

		auto planSwap = a_root;
		swapFirstTwo(At(planSwap, "patch_plans").items[0]);
		ExpectFailure(planSwap, "a reordered patch plan", LoadFailureCode::kNotCanonical);

		auto checkSwap = a_root;
		swapFirstTwo(At(At(At(checkSwap, "byte_proof"), "native_proof"), "checks").items[0]);
		ExpectFailure(checkSwap, "a reordered native check", LoadFailureCode::kNotCanonical);

		auto identitySwap = a_root;
		swapFirstTwo(At(At(identitySwap, "stock_identities").items[0], "outcomes").items[0]);
		ExpectFailure(identitySwap, "a reordered blob outcome", LoadFailureCode::kNotCanonical);

		auto missingKey = a_root;
		Erase(missingKey, "fallback_graph");
		ExpectFailure(missingKey, "a dropped root key", LoadFailureCode::kSchemaViolation);

		auto extraKey = a_root;
		extraKey.members.push_back(Member{ "zzz_extra", MakeInteger(1) });
		ExpectFailure(extraKey, "an added root key", LoadFailureCode::kSchemaViolation);

		auto wrongType = a_root;
		Set(wrongType, "schema_version", MakeString("2"));
		ExpectFailure(wrongType, "a string schema version", LoadFailureCode::kSchemaViolation);

		auto nulled = a_root;
		Set(nulled, "schema_version", MakeNull());
		ExpectFailure(nulled, "a null schema version", LoadFailureCode::kSchemaViolation);

		auto samplerTyped = a_root;
		Set(At(At(samplerTyped, "participants"), "SSGI"), "sampler", MakeString("linear"));
		ExpectFailure(samplerTyped, "a typed participant sampler", LoadFailureCode::kSchemaViolation);

		auto planTyped = a_root;
		Set(
			At(At(planTyped, "stock_identities").items[0], "outcomes").items[1],
			"patch_plan",
			MakeString("composite-ambient-bb66b923:SSGI"));
		ExpectFailure(planTyped, "a typed absent patch plan", LoadFailureCode::kSchemaViolation);
	}

	void TestPlanAttacks(const Value& a_root)
	{
		auto dropped = a_root;
		At(dropped, "patch_plans").items.pop_back();
		ExpectFailure(dropped, "a dropped plan", LoadFailureCode::kSchemaViolation);

		auto duplicated = a_root;
		At(duplicated, "patch_plans").items.push_back(At(duplicated, "patch_plans").items[0]);
		ExpectFailure(duplicated, "a duplicated plan", LoadFailureCode::kSchemaViolation);

		auto emptied = a_root;
		At(emptied, "patch_plans").items.clear();
		ExpectFailure(emptied, "an emptied plan matrix", LoadFailureCode::kSchemaViolation);

		auto reordered = a_root;
		std::swap(At(reordered, "patch_plans").items[0], At(reordered, "patch_plans").items[1]);
		ExpectFailure(reordered, "a reordered plan matrix", LoadFailureCode::kContractPinMismatch);

		auto crossStock = a_root;
		Set(
			At(crossStock, "patch_plans").items[0],
			"stock_sha256",
			MakeString("c36d04cc8e593d4840607ff3abe1f48cd2ee2db302a16101070f657a608094cd"));
		ExpectFailure(crossStock, "a cross-stock plan transfer", LoadFailureCode::kCommitmentMismatch);

		auto crossSet = a_root;
		Set(
			At(crossSet, "patch_plans").items[0],
			"participants",
			MakeStringArray({ "Wetness" }));
		ExpectFailure(crossSet, "a cross-set plan transfer", LoadFailureCode::kSchemaViolation);

		auto wrongSlot = a_root;
		Set(
			At(At(wrongSlot, "patch_plans").items[0], "added_resource_claims").items[0],
			"register",
			MakeInteger(1));
		ExpectFailure(wrongSlot, "a wrong claimed slot", LoadFailureCode::kSchemaViolation);

		auto wrongDeclaration = a_root;
		Set(
			At(At(wrongDeclaration, "patch_plans").items[0], "patched_resource_declarations").items[0],
			"dimension",
			MakeInteger(4));
		ExpectFailure(
			wrongDeclaration, "a drifted patched declaration", LoadFailureCode::kCommitmentMismatch);

		auto declarationOrder = a_root;
		auto& declarations =
			At(At(declarationOrder, "patch_plans").items[0], "stock_resource_declarations").items;
		std::swap(declarations[0], declarations[1]);
		ExpectFailure(
			declarationOrder, "an unordered declaration array", LoadFailureCode::kSchemaViolation);

		auto fallbackClaim = a_root;
		Set(At(fallbackClaim, "patch_plans").items[0], "fallback_proven", MakeBoolean(true));
		ExpectFailure(fallbackClaim, "a claimed fallback", LoadFailureCode::kSchemaViolation);

		auto statusRaise = a_root;
		Set(At(statusRaise, "patch_plans").items[0], "target_status", MakeString("PASS"));
		ExpectFailure(statusRaise, "a raised plan target status", LoadFailureCode::kSchemaViolation);

		auto withoutNative = a_root;
		Set(
			At(withoutNative, "patch_plans").items[0],
			"native_proof_receipt_sha256",
			MakeString(std::string(64, '0')));
		ExpectFailure(
			withoutNative,
			"a PASS plan without its native receipt",
			LoadFailureCode::kContractPinMismatch);

		auto combinedChain = a_root;
		auto& ordering =
			At(At(At(combinedChain, "patch_plans").items[2], "static_proof"), "ordering").items;
		Check(ordering.size() == 3, "the combined plan does not build directly from stock");
		ordering.pop_back();
		ExpectFailure(combinedChain, "a chained combined plan", LoadFailureCode::kSchemaViolation);

		auto editOrder = a_root;
		auto& edits = At(At(editOrder, "patch_plans").items[0], "edits").items;
		std::swap(edits[0], edits[1]);
		ExpectFailure(editOrder, "a reordered edit list", LoadFailureCode::kSchemaViolation);

		auto editOverlap = a_root;
		Set(
			At(At(editOverlap, "patch_plans").items[0], "edits").items[1],
			"executable_dword",
			MakeInteger(51));
		ExpectFailure(editOverlap, "an overlapping edit range", LoadFailureCode::kSchemaViolation);

		auto preimageDrift = a_root;
		Set(
			At(At(At(preimageDrift, "patch_plans").items[0], "edits").items[0], "preimage"),
			"offset",
			MakeInteger(0));
		ExpectFailure(preimageDrift, "a detached edit preimage", LoadFailureCode::kSchemaViolation);

		auto scratchRole = a_root;
		Set(
			At(At(scratchRole, "patch_plans").items[0], "scratch_components").items[0],
			"role",
			MakeString("stock-scratch"));
		ExpectFailure(scratchRole, "a split scratch row", LoadFailureCode::kSchemaViolation);

		auto scratchVocabulary = a_root;
		Set(
			At(At(scratchVocabulary, "patch_plans").items[0], "scratch_components").items[0],
			"role",
			MakeString("stock-scratch"));
		Set(
			At(At(At(scratchVocabulary, "patch_plans").items[0], "static_proof"), "scratch_components")
				.items[0],
			"role",
			MakeString("stock-scratch"));
		ExpectFailure(scratchVocabulary, "a drifted scratch role", LoadFailureCode::kSchemaViolation);

		auto gateDrop = a_root;
		At(At(At(gateDrop, "patch_plans").items[0], "static_proof"), "gates").items.pop_back();
		ExpectFailure(gateDrop, "a dropped static gate", LoadFailureCode::kSchemaViolation);

		auto maskDrift = a_root;
		Set(
			At(At(maskDrift, "patch_plans").items[0], "static_proof"),
			"output_masks",
			MakeStringArray({ "xyzw" }));
		ExpectFailure(maskDrift, "a drifted output mask set", LoadFailureCode::kSchemaViolation);

		auto guardDrift = a_root;
		At(At(At(guardDrift, "patch_plans").items[0], "static_proof"), "guarded_stock_write_dwords")
			.members.clear();
		ExpectFailure(
			guardDrift, "a dropped guarded stock write set", LoadFailureCode::kSchemaViolation);
	}

	void TestClosureAttacks(const Value& a_root)
	{
		auto occurrenceDrop = a_root;
		At(occurrenceDrop, "occurrences").items.pop_back();
		ExpectFailure(occurrenceDrop, "a dropped occurrence", LoadFailureCode::kSchemaViolation);

		auto identityDrop = a_root;
		At(identityDrop, "stock_identities").items.pop_back();
		ExpectFailure(identityDrop, "a dropped stock identity", LoadFailureCode::kSchemaViolation);

		auto verdictRaise = a_root;
		Set(
			At(At(verdictRaise, "stock_identities").items[0], "outcomes").items[1],
			"verdict",
			MakeString("PASS"));
		ExpectFailure(verdictRaise, "a raised blob verdict", LoadFailureCode::kSchemaViolation);

		auto statusRaise = a_root;
		Set(
			At(At(statusRaise, "occurrences").items[0], "participant_status"),
			"SSGI",
			MakeString("PASS"));
		ExpectFailure(statusRaise, "a raised occurrence status", LoadFailureCode::kSchemaViolation);

		auto tallyDrift = a_root;
		Set(
			At(At(At(At(tallyDrift, "byte_proof"), "per_participant_set"), "SSGI"), "by_blob"),
			"PASS",
			MakeInteger(3));
		ExpectFailure(tallyDrift, "a drifted participant tally", LoadFailureCode::kSchemaViolation);

		auto collisionDrift = a_root;
		Set(At(At(collisionDrift, "byte_proof"), "collision_counts"), "t0_blobs", MakeInteger(31));
		ExpectFailure(collisionDrift, "a drifted collision tally", LoadFailureCode::kSchemaViolation);

		auto denominatorDrift = a_root;
		Set(At(denominatorDrift, "denominator"), "composite_occurrences", MakeInteger(179));
		ExpectFailure(denominatorDrift, "a drifted denominator", LoadFailureCode::kContractPinMismatch);

		auto censusDrift = a_root;
		Set(At(censusDrift, "census"), "structural_count", MakeInteger(3938));
		ExpectFailure(censusDrift, "a drifted census", LoadFailureCode::kContractPinMismatch);

		auto keyDomainDrift = a_root;
		Set(At(keyDomainDrift, "classification"), "key_domain", MakeString("engine_lookup_psid"));
		ExpectFailure(keyDomainDrift, "a runtime key domain", LoadFailureCode::kContractPinMismatch);

		auto resolverDrift = a_root;
		Set(At(resolverDrift, "route_admission"), "resolver", MakeString("exact"));
		ExpectFailure(resolverDrift, "an admitted resolver", LoadFailureCode::kContractPinMismatch);

		auto admissibleDrift = a_root;
		Set(At(admissibleDrift, "release_policy"), "runtime_admissible", MakeBoolean(true));
		ExpectFailure(
			admissibleDrift, "a runtime-admissible policy", LoadFailureCode::kContractPinMismatch);

		auto ownershipDrift = a_root;
		At(ownershipDrift, "route_admission").members.push_back(Member{ "zzz_owner", MakeString("SSGI") });
		ExpectFailure(ownershipDrift, "an ownership field", LoadFailureCode::kContractPinMismatch);

		auto normalizerDrift = a_root;
		Set(
			At(At(normalizerDrift, "classification"), "normalizer_diagnostic").items[0],
			"fxp_ordinal",
			MakeInteger(3405));
		ExpectFailure(normalizerDrift, "a drifted normalizer row", LoadFailureCode::kContractPinMismatch);

		auto staticGateDrift = a_root;
		At(At(staticGateDrift, "byte_proof"), "static_gates").items.pop_back();
		ExpectFailure(staticGateDrift, "a dropped byte-proof gate", LoadFailureCode::kSchemaViolation);

		auto artifactMutationDrift = a_root;
		At(At(artifactMutationDrift, "byte_proof"), "artifact_validation_mutations").items.pop_back();
		ExpectFailure(
			artifactMutationDrift, "a dropped artifact mutation", LoadFailureCode::kSchemaViolation);

		auto nativeMutantDrift = a_root;
		At(At(At(nativeMutantDrift, "byte_proof"), "native_proof"), "mutants").items.pop_back();
		ExpectFailure(nativeMutantDrift, "a dropped native mutant", LoadFailureCode::kSchemaViolation);

		auto nativeCheckDrift = a_root;
		At(At(At(nativeCheckDrift, "byte_proof"), "native_proof"), "checks").items.pop_back();
		ExpectFailure(nativeCheckDrift, "a dropped native check", LoadFailureCode::kSchemaViolation);

		auto staticMutationDrift = a_root;
		At(At(staticMutationDrift, "byte_proof"), "static_mutations").items.pop_back();
		ExpectFailure(
			staticMutationDrift, "a dropped static mutation", LoadFailureCode::kSchemaViolation);

		auto duplicateRegister = a_root;
		auto& identityDeclarations =
			At(At(duplicateRegister, "stock_identities").items[0], "resource_declarations").items;
		identityDeclarations.push_back(identityDeclarations.back());
		ExpectFailure(
			duplicateRegister, "a duplicated resource register", LoadFailureCode::kSchemaViolation);

		auto runtimeIdentity = a_root;
		At(runtimeIdentity, "occurrences").items[0].members.push_back(
			Member{ "zzz_engine_lookup_psid", MakeString("0x00000001") });
		ExpectFailure(
			runtimeIdentity, "a runtime identity field", LoadFailureCode::kSchemaViolation);
	}

	void TestCommitmentAttacks(const Value& a_root)
	{
		auto occurrenceSet = a_root;
		Set(At(occurrenceSet, "receipts"), "occurrence_set_sha256", MakeString(std::string(64, 'a')));
		ExpectFailure(
			occurrenceSet, "a forged occurrence set commitment", LoadFailureCode::kCommitmentMismatch);

		auto claims = a_root;
		Set(
			At(claims, "receipts"),
			"artifact_claims_commitment_sha256",
			MakeString(std::string(64, 'b')));
		ExpectFailure(
			claims, "a forged artifact claims commitment", LoadFailureCode::kCommitmentMismatch);

		auto aggregate = a_root;
		Set(
			At(aggregate, "receipts"),
			"aggregate_declaration_commitment_sha256",
			MakeString(std::string(64, 'c')));
		ExpectFailure(
			aggregate,
			"a forged aggregate declaration commitment",
			LoadFailureCode::kCommitmentMismatch);

		auto contracts = a_root;
		Set(At(contracts, "receipts"), "contracts_sha256", MakeString(std::string(64, 'd')));
		ExpectFailure(
			contracts, "a substituted opaque contracts digest", LoadFailureCode::kContractPinMismatch);

		auto identityCommitment = a_root;
		Set(
			At(identityCommitment, "stock_identities").items[0],
			"declaration_commitment_sha256",
			MakeString(std::string(64, 'e')));
		ExpectFailure(
			identityCommitment,
			"a forged identity declaration commitment",
			LoadFailureCode::kCommitmentMismatch);

		auto planReceipt = a_root;
		Set(
			At(At(planReceipt, "patch_plans").items[0], "receipts"),
			"plan_sha256",
			MakeString(std::string(64, 'f')));
		ExpectFailure(planReceipt, "a forged plan receipt", LoadFailureCode::kCommitmentMismatch);

		auto nativeProofReceipt = a_root;
		Set(At(nativeProofReceipt, "receipts"), "native_proof_sha256", MakeString(std::string(64, 'a')));
		ExpectFailure(
			nativeProofReceipt, "a forged native proof receipt", LoadFailureCode::kCommitmentMismatch);

		// Rehash-after-tamper: a self-consistent check row still loses the semantic commitment.
		auto tamperedCheck = a_root;
		auto& native = At(At(tamperedCheck, "byte_proof"), "native_proof");
		auto& checkRow = At(native, "checks").items[0];
		Set(checkRow, "detail", MakeString("all output channels are finite and pretty"));
		Set(checkRow, "evidence_receipt_sha256", MakeString(CheckEvidenceReceipt(native, checkRow)));
		ExpectFailure(tamperedCheck, "a rehashed native check", LoadFailureCode::kCommitmentMismatch);

		// A field outside the semantic projection still loses the full-row contract commitment.
		auto tamperedInputs = a_root;
		auto& inputNative = At(At(tamperedInputs, "byte_proof"), "native_proof");
		auto& inputRow = At(inputNative, "checks").items[0];
		At(inputRow, "input_receipts").items[0] = MakeString(std::string(64, '9'));
		Set(inputRow, "evidence_receipt_sha256", MakeString(CheckEvidenceReceipt(inputNative, inputRow)));
		ExpectFailure(
			tamperedInputs, "a rehashed check input receipt", LoadFailureCode::kCommitmentMismatch);

		auto tamperedStatic = a_root;
		auto& staticRow = At(At(tamperedStatic, "byte_proof"), "static_mutations").items[0];
		Set(staticRow, "detail", MakeString("feature-resource: rewritten detail"));
		Set(staticRow, "evidence_receipt_sha256", MakeString(StaticMutationReceipt(staticRow)));
		Set(
			At(tamperedStatic, "receipts"),
			"artifact_claims_commitment_sha256",
			MakeString(ArtifactClaimsCommitment(tamperedStatic)));
		ExpectFailure(
			tamperedStatic, "a rehashed static mutant", LoadFailureCode::kContractPinMismatch);

		auto tamperedFamily = a_root;
		auto& familyRow = At(At(tamperedFamily, "byte_proof"), "static_mutations").items[0];
		Set(familyRow, "bucket", MakeString("ssgi-slot"));
		Set(familyRow, "evidence_receipt_sha256", MakeString(StaticMutationReceipt(familyRow)));
		ExpectFailure(
			tamperedFamily, "a rehashed static mutation family", LoadFailureCode::kSchemaViolation);

		auto toolchain = a_root;
		Set(
			At(At(At(toolchain, "byte_proof"), "native_proof"), "harness_build"),
			"compiler_version",
			MakeString("19.51.36253.0"));
		ExpectFailure(toolchain, "a drifted toolchain identity", LoadFailureCode::kContractPinMismatch);

		auto semanticCommitment = a_root;
		Set(
			At(At(At(semanticCommitment, "byte_proof"), "native_proof"), "semantic_commitments"),
			"mutants_sha256",
			MakeString(std::string(64, '1')));
		ExpectFailure(
			semanticCommitment,
			"a forged mutant semantic commitment",
			LoadFailureCode::kCommitmentMismatch);

		auto nativePlanReceipt = a_root;
		Set(
			At(At(At(nativePlanReceipt, "byte_proof"), "native_proof"), "plan_receipts"),
			"composite-ambient-bb66b923:SSGI",
			MakeString(std::string(64, '2')));
		ExpectFailure(
			nativePlanReceipt, "a forged native plan receipt", LoadFailureCode::kCommitmentMismatch);

		// A fully self-consistent forged subtree still loses the reviewed full-row contract pin.
		auto chained = a_root;
		auto& chainedNative = At(At(chained, "byte_proof"), "native_proof");
		auto& chainedRow = At(chainedNative, "checks").items[0];
		At(chainedRow, "input_receipts").items[0] = MakeString(std::string(64, '9'));
		Set(
			chainedRow,
			"evidence_receipt_sha256",
			MakeString(CheckEvidenceReceipt(chainedNative, chainedRow)));
		Set(At(chained, "receipts"), "native_proof_sha256", MakeString(NativeProofReceipt(chainedNative)));
		Set(
			At(chained, "receipts"),
			"artifact_claims_commitment_sha256",
			MakeString(ArtifactClaimsCommitment(chained)));
		ExpectFailure(
			chained,
			"a fully rehashed native check subtree",
			LoadFailureCode::kContractPinMismatch);
	}

	// The build receipt preimage is 22 raw name=value lines, rebuilt independently here.
	void TestBuildReceiptPreimage(const Value& a_root)
	{
		static constexpr std::array<std::string_view, 22> order{
			"schema", "schema_version", "selected_source_object", "source_sha256",
			"selected_build_script_object", "build_script_sha256", "canonical_source_path",
			"canonical_working_directory", "canonical_temporary_directory", "object_name",
			"binary_name", "compiler_sha256", "compiler_length", "compiler_version", "linker_sha256",
			"linker_length", "linker_version", "compile_flags", "link_flags",
			"environment_variables_cleared", "binary_sha256", "binary_length"
		};
		const auto& build = *a_root.Find("byte_proof")->Find("native_proof")->Find("harness_build");
		Check(build.members.size() == 23, "the harness build does not carry 23 keys");
		std::string payload;
		for (const auto key : order) {
			const auto* value = build.Find(key);
			Check(value != nullptr, "the harness build is missing a preimage field");
			payload += key;
			payload.push_back('=');
			if (key == "environment_variables_cleared") {
				bool first = true;
				for (const auto& item : value->items) {
					if (!first)
						payload.push_back(',');
					first = false;
					payload += item.text;
				}
			} else if (value->kind == ValueKind::kString) {
				payload += value->text;
			} else {
				payload += value->lexeme;
			}
			payload.push_back('\n');
		}
		Check(
			RequireSha256Hex(payload) == build.Find("receipt_sha256")->text,
			"the reviewed 22-line build receipt preimage does not reproduce the published receipt");
	}

	void TestImmutableResult(const std::filesystem::path& a_path, const std::string& a_bytes)
	{
		const auto first = LoadPinnedSnapshot(a_path);
		Check(first.has_value(), "the first pinned load failed");
		const auto second = LoadPinnedSnapshot(a_path);
		Check(second.has_value(), "the second pinned load failed");
		Check(first->get() != second->get(), "the loader returned a cached snapshot");
		Check(
			(*first)->report.size() == (*second)->report.size(),
			"report row counts are not deterministic");
		for (std::size_t index = 0; index < (*first)->report.size(); ++index) {
			Check(
				(*first)->report[index].key == (*second)->report[index].key &&
					(*first)->report[index].value == (*second)->report[index].value,
				"report rows are not deterministic");
		}

		const auto path = std::filesystem::temp_directory_path() / "fo4cs-composite-invalid.json";
		auto broken = a_bytes;
		broken[broken.size() / 3] = broken[broken.size() / 3] == 'a' ? 'b' : 'a';
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			stream.write(broken.data(), static_cast<std::streamsize>(broken.size()));
		}
		const auto failed = LoadPinnedSnapshot(path);
		Check(!failed.has_value(), "a tampered publication was accepted after a valid load");
		Check((*first)->inventory.fileSha256 == kArtifactSha256, "the earlier snapshot was mutated");
		std::filesystem::remove(path);

		const auto& rows = (*first)->report;
		const auto keyed = [&rows](std::string_view a_key) {
			const auto found = std::find_if(
				rows.begin(), rows.end(), [a_key](const fo4cs::offline::ReportRow& a_row) {
					return a_row.key == a_key;
				});
			Check(found != rows.end(), "report row is absent: " + std::string(a_key));
			return found->value;
		};
		Check(keyed("runtime_admissible") == "false", "the report claims runtime admissibility");
		Check(keyed("resolver") == "no-match", "the report claims a resolver match");
		Check(keyed("suppression") == "none", "the report claims suppression");
		Check(keyed("ownership") == "absent", "the report claims ownership");
		Check(keyed("join_receipts") == "0", "the report claims join receipts");
		Check(keyed("runtime_observations") == "0", "the report claims runtime observations");
		Check(keyed("runtime_routes_admitted") == "0", "the report claims admitted routes");
		Check(keyed("runtime_routes_exclusive") == "0", "the report claims exclusive routes");
	}

	void TestHostIsolation(const std::filesystem::path& a_repoRoot)
	{
		const auto offline = a_repoRoot / "offline" / "BsdfCompositeAmbientDxbcPatch";
		Check(std::filesystem::is_directory(offline), "the offline consumer directory is missing");
		static constexpr std::array<std::string_view, 5> allowedLocalIncludes{
			"Artifact.h", "ArtifactInternal.h", "CanonicalJson.h", "ContractPins.h", "Sha256.h"
		};
		// Windows.h and bcrypt.h are the mandated host hash provider; nothing else may appear.
		static constexpr std::array<std::string_view, 8> forbiddenSystemIncludes{
			"d3d", "dxgi", "f4se", "spdlog", "nlohmann", "imgui", "detours", "rel/"
		};
		static constexpr std::array<std::string_view, 4> forbiddenTokens{
			"PixelShaderSwap", "ShaderInjection", "serializer_receipt", "PowerShell"
		};
		std::size_t inspected = 0;
		for (const auto& entry : std::filesystem::directory_iterator(offline)) {
			if (!entry.is_regular_file())
				continue;
			++inspected;
			const auto text = ReadFile(entry.path());
			for (const auto token : forbiddenTokens) {
				Check(
					text.find(token) == std::string::npos,
					"an offline source references " + std::string(token));
			}
			std::size_t position = 0;
			while ((position = text.find("#include", position)) != std::string::npos) {
				const auto end = text.find('\n', position);
				const auto line = text.substr(position, end - position);
				position = end == std::string::npos ? text.size() : end;
				const auto quote = line.find('"');
				if (quote != std::string::npos) {
					const auto close = line.find('"', quote + 1);
					const auto name = line.substr(quote + 1, close - quote - 1);
					Check(
						std::find(allowedLocalIncludes.begin(), allowedLocalIncludes.end(), name) !=
							allowedLocalIncludes.end(),
						"an offline source includes " + name);
					continue;
				}
				std::string lowered(line);
				std::transform(
					lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char a_character) {
						return static_cast<char>(std::tolower(a_character));
					});
				for (const auto token : forbiddenSystemIncludes) {
					Check(
						lowered.find(token) == std::string::npos,
						"an offline source includes a forbidden system header: " + line);
				}
			}
		}
		Check(inspected == 9, "the offline consumer file inventory changed");
	}

	void TestV1Independence(const std::filesystem::path& a_repoRoot)
	{
		const auto v1 = a_repoRoot / "package" / "F4SE" / "Plugins" / "FO4CommunityShaders" /
		                "ScreenSpaceShadows" / "sss-dxbc-patch-plans.json";
		Check(std::filesystem::is_regular_file(v1), "the v1 SSS publication is missing");
		Check(RequireSha256Hex(ReadFile(v1)) == kSssArtifactSha256, "the v1 SSS publication changed");
	}
}

int main(int a_argc, char** a_argv)
{
	if (a_argc != 2) {
		std::cerr << "usage: BsdfCompositeAmbientDxbcPatchTests <artifact>\n";
		return 2;
	}
	const std::filesystem::path artifact(a_argv[1]);
	try {
		const auto bytes = ReadFile(artifact);
		const auto document = ParseCanonical(bytes);
		Check(document.has_value(), "the publication is not canonical");
		const auto& root = document->Root();
		const auto repoRoot =
			artifact.parent_path().parent_path().parent_path().parent_path();

		TestPinnedLoad(artifact, bytes);
		std::cout << "PASS: pinned publication load and inventory\n";
		TestWholeBytePin(bytes);
		std::cout << "PASS: whole-byte pin before decode\n";
		TestSemanticByteDrift(root);
		std::cout << "PASS: semantically equal byte drift rejection\n";
		TestCanonicalRoundTrip(bytes, *document);
		std::cout << "PASS: lexeme-preserving canonical round trip\n";
		TestCanonicalAttacks(bytes);
		std::cout << "PASS: canonical byte attacks\n";
		TestImmutableResult(artifact, bytes);
		std::cout << "PASS: immutable result and deterministic report\n";
		TestHostIsolation(repoRoot);
		std::cout << "PASS: host-only source isolation\n";
		TestV1Independence(repoRoot);
		std::cout << "PASS: v1 publication independence\n";

		struct Test
		{
			const char* name;
			void (*run)(const Value&);
		};
		const Test tests[]{
			{ "key order and exact type attacks", &TestKeyOrderAttacks },
			{ "plan matrix and mechanical attacks", &TestPlanAttacks },
			{ "semantic closure attacks", &TestClosureAttacks },
			{ "commitment and transfer attacks", &TestCommitmentAttacks },
			{ "reviewed build receipt preimage", &TestBuildReceiptPreimage }
		};
		for (const auto& test : tests) {
			test.run(root);
			std::cout << "PASS: " << test.name << '\n';
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
