#include "ArtifactInternal.h"

#include "CanonicalJson.h"
#include "ContractPins.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4cs::offline::internal
{
	namespace
	{
		using canonical::CompactArray;
		using canonical::CompactNode;
		using canonical::CompactObject;
		using canonical::CompactString;
		using canonical::Value;
		using canonical::ValueKind;

		constexpr std::array<std::string_view, 16> kRootKeys{
			"archive", "byte_proof", "census", "classification", "denominator",
			"engine_scope", "fallback_graph", "occurrences", "participants",
			"patch_plans", "receipts", "release_policy", "route_admission",
			"schema", "schema_version", "stock_identities"
		};

		constexpr std::array<std::string_view, 4> kParticipantSetKeys{
			"stock", "SSGI", "Wetness", "SSGI+Wetness"
		};

		constexpr std::array<std::string_view, 4> kSortedParticipantSetKeys{
			"SSGI", "SSGI+Wetness", "Wetness", "stock"
		};

		constexpr std::array<std::string_view, 3> kOrderingCatalog{
			"Wetness -> stock t9 sample",
			"stock t9 consumer -> SSGI -> stock fog",
			"combined product built directly from stock"
		};

		constexpr std::string_view kNativeStockBound = "native-stock-bound";
		constexpr std::string_view kFormatRealisticSynthetic = "format-realistic-synthetic";
		constexpr std::string_view kUnbound = "unbound";
		constexpr std::string_view kVerdictPass = "PASS";
		constexpr std::string_view kVerdictFail = "FAIL";
		constexpr std::string_view kVerdictUnproven = "UNPROVEN";

		std::string Join(std::string_view a_label, std::string_view a_key)
		{
			return std::string(a_label) + "." + std::string(a_key);
		}

		[[noreturn]] void Violate(std::string_view a_label, std::string_view a_reason)
		{
			Reject(LoadFailureCode::kSchemaViolation, std::string(a_label) + " " + std::string(a_reason));
		}

		const Value& Child(const Value& a_parent, std::string_view a_key, std::string_view a_label)
		{
			const auto* value = a_parent.Find(a_key);
			if (value == nullptr)
				Violate(Join(a_label, a_key), "is absent");
			return *value;
		}

		const Value& Typed(const Value& a_value, ValueKind a_kind, std::string_view a_label)
		{
			if (a_value.kind != a_kind)
				Violate(a_label, "has the wrong exact JSON type");
			return a_value;
		}

		const Value& ObjectAt(const Value& a_parent, std::string_view a_key, std::string_view a_label)
		{
			return Typed(Child(a_parent, a_key, a_label), ValueKind::kObject, Join(a_label, a_key));
		}

		const Value& ArrayAt(const Value& a_parent, std::string_view a_key, std::string_view a_label)
		{
			return Typed(Child(a_parent, a_key, a_label), ValueKind::kArray, Join(a_label, a_key));
		}

		std::string_view StringAt(const Value& a_parent, std::string_view a_key, std::string_view a_label)
		{
			return Typed(Child(a_parent, a_key, a_label), ValueKind::kString, Join(a_label, a_key)).text;
		}

		std::int64_t IntegerAt(const Value& a_parent, std::string_view a_key, std::string_view a_label)
		{
			return Typed(Child(a_parent, a_key, a_label), ValueKind::kInteger, Join(a_label, a_key)).integer;
		}

		bool BooleanAt(const Value& a_parent, std::string_view a_key, std::string_view a_label)
		{
			return Typed(Child(a_parent, a_key, a_label), ValueKind::kBoolean, Join(a_label, a_key)).boolean;
		}

		void RequireKeys(
			const Value& a_object,
			std::span<const std::string_view> a_keys,
			std::string_view a_label)
		{
			Typed(a_object, ValueKind::kObject, a_label);
			const auto actual = a_object.Keys();
			if (actual.size() != a_keys.size() ||
				!std::equal(actual.begin(), actual.end(), a_keys.begin())) {
				Violate(a_label, "does not carry the exact reviewed key set");
			}
		}

		void RequireText(std::string_view a_actual, std::string_view a_expected, std::string_view a_label)
		{
			if (a_actual != a_expected)
				Violate(a_label, "is not the exact reviewed value");
		}

		void RequireNumber(std::int64_t a_actual, std::int64_t a_expected, std::string_view a_label)
		{
			if (a_actual != a_expected)
				Violate(a_label, "is not the exact reviewed count");
		}

		void RequireTrue(bool a_condition, std::string_view a_label, std::string_view a_reason)
		{
			if (!a_condition)
				Violate(a_label, a_reason);
		}

		bool IsLowerHex(std::string_view a_value, std::size_t a_length)
		{
			return a_value.size() == a_length &&
			       std::all_of(a_value.begin(), a_value.end(), [](char a_character) {
					   return (a_character >= '0' && a_character <= '9') ||
			                  (a_character >= 'a' && a_character <= 'f');
				   });
		}

		std::string_view DigestAt(
			const Value& a_parent,
			std::string_view a_key,
			std::string_view a_label,
			std::size_t a_length = 64)
		{
			const auto value = StringAt(a_parent, a_key, a_label);
			if (!IsLowerHex(value, a_length))
				Violate(Join(a_label, a_key), "is not a canonical digest");
			return value;
		}

		void RequireArchiveKey(std::string_view a_value, std::string_view a_label)
		{
			if (a_value.size() != 10 || a_value.substr(0, 2) != "0x" || !IsLowerHex(a_value.substr(2), 8))
				Violate(a_label, "is not an archive fxp key");
		}

		void RequirePin(
			std::string_view a_actual,
			std::string_view a_expected,
			std::string_view a_label)
		{
			if (a_actual != a_expected) {
				Reject(
					LoadFailureCode::kContractPinMismatch,
					std::string(a_label) + " does not match its reviewed pin");
			}
		}

		void RequireCensus(
			std::string_view a_actual,
			std::string_view a_expected,
			std::string_view a_label)
		{
			if (a_actual != a_expected) {
				Reject(
					LoadFailureCode::kSchemaViolation,
					std::string(a_label) + " does not carry the exact reviewed scalar kinds");
			}
		}

		void RequireCommitment(
			std::string_view a_actual,
			std::string_view a_expected,
			std::string_view a_label)
		{
			if (a_actual != a_expected) {
				Reject(
					LoadFailureCode::kCommitmentMismatch,
					std::string(a_label) + " is not rederivable from the publication");
			}
		}

		std::string Rcpt(const std::string& a_preimage)
		{
			return RequireSha256Hex(a_preimage);
		}

		std::vector<std::string> TextArray(const Value& a_array, std::string_view a_label)
		{
			std::vector<std::string> values;
			values.reserve(a_array.items.size());
			for (std::size_t index = 0; index < a_array.items.size(); ++index) {
				const auto label = std::string(a_label) + "[" + std::to_string(index) + "]";
				values.emplace_back(Typed(a_array.items[index], ValueKind::kString, label).text);
			}
			return values;
		}

		std::string ParticipantKey(std::span<const std::string> a_participants)
		{
			if (a_participants.empty())
				return "stock";
			std::string key;
			for (const auto& participant : a_participants) {
				if (!key.empty())
					key.push_back('+');
				key += participant;
			}
			return key;
		}

		std::size_t ParticipantSetIndex(std::string_view a_key, std::string_view a_label)
		{
			for (std::size_t index = 0; index < kParticipantSetKeys.size(); ++index) {
				if (kParticipantSetKeys[index] == a_key)
					return index;
			}
			Violate(a_label, "is not a reviewed participant set");
		}

		// Canonical order is SSGI before Wetness; no other participant exists.
		void RequireCanonicalParticipants(
			std::span<const std::string> a_participants,
			std::string_view a_label)
		{
			RequireTrue(!a_participants.empty(), a_label, "is empty");
			RequireTrue(a_participants.size() <= 2, a_label, "carries an unknown participant count");
			if (a_participants.size() == 1) {
				RequireTrue(
					a_participants[0] == "SSGI" || a_participants[0] == "Wetness",
					a_label,
					"carries an unknown participant");
				return;
			}
			RequireTrue(
				a_participants[0] == "SSGI" && a_participants[1] == "Wetness",
				a_label,
				"is not in canonical participant order");
		}

		struct DeclarationRow
		{
			std::int64_t reg = 0;
			std::int64_t dimension = 0;
			std::string returnToken;
		};

		std::vector<DeclarationRow> ValidateDeclarations(const Value& a_array, std::string_view a_label)
		{
			static constexpr std::array<std::string_view, 3> keys{ "dimension", "register", "return_token" };
			std::vector<DeclarationRow> rows;
			rows.reserve(a_array.items.size());
			std::int64_t previous = -1;
			for (std::size_t index = 0; index < a_array.items.size(); ++index) {
				const auto label = std::string(a_label) + "[" + std::to_string(index) + "]";
				const auto& row = a_array.items[index];
				RequireKeys(row, keys, label);
				DeclarationRow declaration;
				declaration.reg = IntegerAt(row, "register", label);
				declaration.dimension = IntegerAt(row, "dimension", label);
				const auto token = StringAt(row, "return_token", label);
				if (token.size() != 10 || token.substr(0, 2) != "0x" || !IsLowerHex(token.substr(2), 8))
					Violate(Join(label, "return_token"), "is not a resource return token");
				declaration.returnToken = token;
				RequireTrue(declaration.reg > previous, label, "declarations are not unique and ordered");
				previous = declaration.reg;
				rows.push_back(std::move(declaration));
			}
			return rows;
		}

		struct Outcome
		{
			std::string participantSet;
			std::string verdict;
			std::string reason;
			std::string evidenceClass;
			std::string patchPlan;
			bool hasPatchPlan = false;
		};

		struct Identity
		{
			std::string stockSha256;
			std::string stockSha1;
			std::string target;
			std::int64_t stockLength = 0;
			std::vector<DeclarationRow> declarations;
			std::vector<Outcome> outcomes;
			std::size_t occurrences = 0;
			std::string declarationCommitment;
		};

		struct Plan
		{
			std::string planId;
			std::string target;
			std::string stockSha256;
			std::string participantKey;
			std::vector<std::string> participants;
			std::string proofStatus;
			std::string patchedSha256;
			std::string nativeProofReceipt;
			std::vector<DeclarationRow> stockDeclarations;
			std::int64_t stockLength = 0;
		};

		std::string TargetFor(std::string_view a_stockSha256)
		{
			for (const auto& stock : pins::kStockContracts) {
				if (stock.stockSha256 == a_stockSha256)
					return std::string(stock.target);
			}
			return "composite-stock-" + std::string(a_stockSha256.substr(0, 8));
		}

		Outcome DeriveOutcome(
			const Identity& a_identity,
			std::size_t a_setIndex,
			const std::map<std::string, const Plan*>& a_plans)
		{
			Outcome outcome;
			outcome.participantSet = kParticipantSetKeys[a_setIndex];
			if (a_setIndex == 0) {
				outcome.verdict = kVerdictPass;
				outcome.reason = "exact-stock-no-edit";
				outcome.evidenceClass = kNativeStockBound;
				return outcome;
			}
			const bool declaresT0 = std::any_of(
				a_identity.declarations.begin(),
				a_identity.declarations.end(),
				[](const DeclarationRow& a_row) { return a_row.reg == 0; });
			if (outcome.participantSet != "Wetness" && declaresT0) {
				outcome.verdict = kVerdictFail;
				outcome.reason = "stock-declares-required-t0";
				outcome.evidenceClass = kNativeStockBound;
				return outcome;
			}
			const auto key = a_identity.stockSha256 + "|" + outcome.participantSet;
			const auto found = a_plans.find(key);
			if (found == a_plans.end()) {
				outcome.verdict = kVerdictUnproven;
				outcome.reason = "no-exact-mechanical-plan";
				outcome.evidenceClass = kUnbound;
				return outcome;
			}
			const bool passed = found->second->proofStatus == kVerdictPass;
			outcome.verdict = passed ? std::string(kVerdictPass) : std::string(kVerdictUnproven);
			outcome.reason = passed ? "exact-static-and-native-byte-proof" : "exact-plan-not-fully-proven";
			outcome.evidenceClass = passed ? std::string(kNativeStockBound) : std::string(kUnbound);
			outcome.patchPlan = found->second->planId;
			outcome.hasPatchPlan = true;
			return outcome;
		}

		void ValidateReceipts(
			const Value& a_root,
			std::span<const std::string> a_occurrenceSetRows,
			std::span<const std::string> a_aggregateRows,
			std::span<const std::string> a_planReceipts,
			std::span<const std::string> a_staticProofReceipts)
		{
			const auto& receipts = ObjectAt(a_root, "receipts", "artifact");
			static constexpr std::array<std::string_view, 11> receiptKeys{
				"aggregate_declaration_commitment_sha256", "artifact_claims_commitment_sha256",
				"census_build_sha256", "census_run_sha256", "contracts_sha256", "native_proof_sha256",
				"occurrence_commitment_sha256", "occurrence_set_sha256", "plan_receipts",
				"scoped_occurrence_commitment_sha256", "static_proof_receipts"
			};
			RequireKeys(receipts, receiptKeys, "artifact.receipts");
			for (const auto key : receiptKeys) {
				if (key == "plan_receipts" || key == "static_proof_receipts")
					continue;
				DigestAt(receipts, key, "artifact.receipts");
			}

			const auto& census = ObjectAt(a_root, "census", "artifact");
			const auto& classification = ObjectAt(a_root, "classification", "artifact");
			RequireText(
				StringAt(receipts, "census_build_sha256", "artifact.receipts"),
				StringAt(census, "build_receipt_sha256", "artifact.census"),
				"artifact.receipts.census_build_sha256");
			RequireText(
				StringAt(receipts, "census_run_sha256", "artifact.receipts"),
				StringAt(census, "run_receipt_sha256", "artifact.census"),
				"artifact.receipts.census_run_sha256");
			RequireText(
				StringAt(receipts, "occurrence_commitment_sha256", "artifact.receipts"),
				StringAt(census, "occurrence_commitment_sha256", "artifact.census"),
				"artifact.receipts.occurrence_commitment_sha256");
			RequireText(
				StringAt(receipts, "scoped_occurrence_commitment_sha256", "artifact.receipts"),
				StringAt(classification, "scoped_occurrence_commitment_sha256", "artifact.classification"),
				"artifact.receipts.scoped_occurrence_commitment_sha256");
			// Opaque provenance: the consumer compares it and never claims to rederive it.
			RequirePin(
				StringAt(receipts, "contracts_sha256", "artifact.receipts"),
				pins::kContractsSha256,
				"artifact.receipts.contracts_sha256");

			RequireCommitment(
				StringAt(receipts, "occurrence_set_sha256", "artifact.receipts"),
				Rcpt(CompactObject({
					{ "schema", CompactString("fo4re.bsdf-composite-ambient-occurrence-set") },
					{ "schema_version", canonical::CompactInteger(2) },
					{ "rows", CompactArray(a_occurrenceSetRows) } })),
				"artifact.receipts.occurrence_set_sha256");
			RequireCommitment(
				StringAt(receipts, "aggregate_declaration_commitment_sha256", "artifact.receipts"),
				Rcpt(CompactArray(a_aggregateRows)),
				"artifact.receipts.aggregate_declaration_commitment_sha256");

			static constexpr std::array<std::string_view, 2> omitted{
				"artifact_claims_commitment_sha256", "contracts_sha256"
			};
			std::vector<std::pair<std::string, std::string>> claimMembers;
			claimMembers.reserve(a_root.members.size());
			for (const auto& member : a_root.members) {
				claimMembers.emplace_back(
					member.key,
					member.key == "receipts" ? canonical::CompactOmit(member.value, omitted) :
					                           CompactNode(member.value));
			}
			RequireCommitment(
				StringAt(receipts, "artifact_claims_commitment_sha256", "artifact.receipts"),
				Rcpt(CompactObject(std::move(claimMembers))),
				"artifact.receipts.artifact_claims_commitment_sha256");

			const auto& planList = ArrayAt(receipts, "plan_receipts", "artifact.receipts");
			const auto& staticList = ArrayAt(receipts, "static_proof_receipts", "artifact.receipts");
			RequireTrue(
				TextArray(planList, "artifact.receipts.plan_receipts") ==
					std::vector<std::string>(a_planReceipts.begin(), a_planReceipts.end()),
				"artifact.receipts.plan_receipts",
				"does not list the plan receipts in plan order");
			RequireTrue(
				TextArray(staticList, "artifact.receipts.static_proof_receipts") ==
					std::vector<std::string>(a_staticProofReceipts.begin(), a_staticProofReceipts.end()),
				"artifact.receipts.static_proof_receipts",
				"does not list the static proof receipts in plan order");
			RequirePin(
				CompactNode(planList), pins::kReceiptPlanReceipts, "artifact.receipts.plan_receipts");
			RequirePin(
				CompactNode(staticList),
				pins::kReceiptStaticProofReceipts,
				"artifact.receipts.static_proof_receipts");

			// The reviewed census pin binds the byte-proof denominators to the receipt commitments.
			const auto& proof = ObjectAt(a_root, "byte_proof", "artifact");
			const auto& collisions = ObjectAt(proof, "collision_counts", "artifact.byte_proof");
			const auto& staticMutations = ArrayAt(proof, "static_mutations", "artifact.byte_proof");
			std::vector<std::pair<std::string, std::string>> censusMembers;
			for (const auto& member : collisions.members)
				censusMembers.emplace_back(member.key, CompactNode(member.value));
			censusMembers.emplace_back(
				"static_mutation_count",
				canonical::CompactInteger(static_cast<std::int64_t>(staticMutations.items.size())));
			censusMembers.emplace_back(
				"static_mutations_commitment_sha256", CompactString(Rcpt(CompactNode(staticMutations))));
			for (const auto key : { "occurrence_set_sha256", "aggregate_declaration_commitment_sha256",
					 "artifact_claims_commitment_sha256" })
				censusMembers.emplace_back(key, CompactNode(Child(receipts, key, "artifact.receipts")));
			RequirePin(
				CompactObject(std::move(censusMembers)),
				pins::kExpectedCensus,
				"artifact reviewed census pin");
		}

		std::string BuildReceiptPayload(const Value& a_harnessBuild)
		{
			static constexpr std::array<std::string_view, 22> order{
				"schema", "schema_version", "selected_source_object", "source_sha256",
				"selected_build_script_object", "build_script_sha256", "canonical_source_path",
				"canonical_working_directory", "canonical_temporary_directory", "object_name",
				"binary_name", "compiler_sha256", "compiler_length", "compiler_version",
				"linker_sha256", "linker_length", "linker_version", "compile_flags", "link_flags",
				"environment_variables_cleared", "binary_sha256", "binary_length"
			};
			std::string payload;
			for (const auto key : order) {
				const auto& value = Child(a_harnessBuild, key, "artifact.byte_proof.native_proof.harness_build");
				payload += key;
				payload.push_back('=');
				if (key == "environment_variables_cleared") {
					bool first = true;
					for (const auto& item : value.items) {
						if (!first)
							payload.push_back(',');
						first = false;
						payload += Typed(item, ValueKind::kString, "harness build environment").text;
					}
				} else if (value.kind == ValueKind::kString) {
					payload += value.text;
				} else {
					payload += value.lexeme;
				}
				payload.push_back('\n');
			}
			return payload;
		}

		void ValidateNativeChecks(const Value& a_native, std::map<std::string, std::size_t>& a_classCounts)
		{
			static constexpr std::array<std::string_view, 10> checkKeys{
				"active_pixels", "check", "detail", "evidence_class", "evidence_receipt_sha256",
				"expected", "input_receipts", "inputs", "kind", "passed"
			};
			const auto executionPlan = StringAt(a_native, "execution_plan_sha256", "artifact.byte_proof.native_proof");
			const auto runReceipt = StringAt(a_native, "run_receipt_sha256", "artifact.byte_proof.native_proof");
			const auto& checks = ArrayAt(a_native, "checks", "artifact.byte_proof.native_proof");
			RequireNumber(
				static_cast<std::int64_t>(checks.items.size()),
				pins::kNativeCheckCount,
				"artifact.byte_proof.native_proof.checks");
			std::set<std::string> seen;
			for (std::size_t index = 0; index < checks.items.size(); ++index) {
				const auto label = "artifact.byte_proof.native_proof.checks[" + std::to_string(index) + "]";
				const auto& row = checks.items[index];
				RequireKeys(row, checkKeys, label);
				const auto identifier = StringAt(row, "check", label);
				RequireTrue(seen.insert(std::string(identifier)).second, Join(label, "check"), "is duplicated");
				const auto kind = StringAt(row, "kind", label);
				const auto& inputs = ArrayAt(row, "inputs", label);
				const auto& inputReceipts = ArrayAt(row, "input_receipts", label);
				RequireTrue(!inputs.items.empty(), Join(label, "inputs"), "is empty");
				RequireNumber(
					static_cast<std::int64_t>(inputReceipts.items.size()),
					static_cast<std::int64_t>(inputs.items.size()),
					Join(label, "input_receipts"));
				for (const auto& value : inputs.items) {
					RequireTrue(
						value.kind == ValueKind::kString && !value.text.empty(),
						Join(label, "inputs"),
						"carries an empty identifier");
				}
				for (const auto& value : inputReceipts.items) {
					RequireTrue(
						value.kind == ValueKind::kString && IsLowerHex(value.text, 64),
						Join(label, "input_receipts"),
						"carries a malformed receipt");
				}
				RequireTrue(BooleanAt(row, "passed", label), Join(label, "passed"), "is not a PASS");
				RequireTrue(!StringAt(row, "detail", label).empty(), Join(label, "detail"), "is empty");
				RequireTrue(
					IntegerAt(row, "active_pixels", label) >= 0,
					Join(label, "active_pixels"),
					"is negative");

				const auto& expected = ObjectAt(row, "expected", label);
				const bool scalarAdd = kind == "ssgi-scalar-add" || kind == "combined-wetness-then-ssgi";
				const bool scalarFold = kind == "wetness-scalar-fold";
				RequireTrue(
					!StringAt(expected, "predicate", Join(label, "expected")).empty(),
					Join(label, "expected.predicate"),
					"is empty");
				if (scalarAdd) {
					static constexpr std::array<std::string_view, 3> keys{
						"bounce_sha256", "predicate", "tolerance"
					};
					RequireKeys(expected, keys, Join(label, "expected"));
					DigestAt(expected, "bounce_sha256", Join(label, "expected"));
					RequireText(
						Typed(Child(expected, "tolerance", Join(label, "expected")), ValueKind::kDouble,
							Join(label, "expected.tolerance"))
							.lexeme,
						"2e-05",
						Join(label, "expected.tolerance"));
				} else if (scalarFold) {
					static constexpr std::array<std::string_view, 5> keys{
						"ndotv", "predicate", "tolerance", "wet_film_sha256", "wetness"
					};
					RequireKeys(expected, keys, Join(label, "expected"));
					DigestAt(expected, "wet_film_sha256", Join(label, "expected"));
					RequireText(
						Typed(Child(expected, "tolerance", Join(label, "expected")), ValueKind::kDouble,
							Join(label, "expected.tolerance"))
							.lexeme,
						"0.00025",
						Join(label, "expected.tolerance"));
					RequireText(
						Typed(Child(expected, "ndotv", Join(label, "expected")), ValueKind::kDouble,
							Join(label, "expected.ndotv"))
							.lexeme,
						"1.0",
						Join(label, "expected.ndotv"));
					RequireText(
						Typed(Child(expected, "wetness", Join(label, "expected")), ValueKind::kDouble,
							Join(label, "expected.wetness"))
							.lexeme,
						"1.0",
						Join(label, "expected.wetness"));
				} else {
					static constexpr std::array<std::string_view, 8> predicateOnlyKinds{
						"finite-output", "stock-nondegenerate", "stock-zero-output", "bitwise-identity",
						"alpha-bitwise-identity", "active-rgb-difference", "binding-preflight-rejection",
						"wetness-shape"
					};
					RequireTrue(
						std::find(predicateOnlyKinds.begin(), predicateOnlyKinds.end(), kind) !=
							predicateOnlyKinds.end(),
						Join(label, "kind"),
						"is not a reviewed native check kind");
					static constexpr std::array<std::string_view, 1> keys{ "predicate" };
					RequireKeys(expected, keys, Join(label, "expected"));
				}

				// The binding identifier rule independently selects the synthetic evidence class.
				const auto evidenceClass = StringAt(row, "evidence_class", label);
				const bool binding = identifier.find(":binding:") != std::string_view::npos;
				RequireText(
					evidenceClass,
					binding ? kFormatRealisticSynthetic : kNativeStockBound,
					Join(label, "evidence_class"));
				++a_classCounts[std::string(evidenceClass)];

				RequireCommitment(
					StringAt(row, "evidence_receipt_sha256", label),
					Rcpt(CompactObject({
						{ "execution_plan_sha256", CompactString(executionPlan) },
						{ "run_receipt_sha256", CompactString(runReceipt) },
						{ "check", CompactNode(Child(row, "check", label)) },
						{ "kind", CompactNode(Child(row, "kind", label)) },
						{ "inputs", CompactNode(inputs) },
						{ "expected", CompactNode(expected) },
						{ "input_receipts", CompactNode(inputReceipts) },
						{ "passed", CompactNode(Child(row, "passed", label)) },
						{ "detail", CompactNode(Child(row, "detail", label)) },
						{ "active_pixels", CompactNode(Child(row, "active_pixels", label)) },
						{ "evidence_class", CompactNode(Child(row, "evidence_class", label)) } })),
					Join(label, "evidence_receipt_sha256"));
			}
		}

		void ValidateNativeMutants(const Value& a_native, std::map<std::string, std::size_t>& a_classCounts)
		{
			static constexpr std::array<std::string_view, 17> mutantKeys{
				"active_output_sha256", "bucket", "detail", "evidence_class", "evidence_receipt_sha256",
				"expected_gate", "failure_signature", "family", "mutant_sha256", "mutation",
				"neutral_identity_holds", "neutral_output_sha256", "participants",
				"reference_output_sha256", "reference_sha256", "rejected", "rejecting_gate"
			};
			const auto executionPlan = StringAt(a_native, "execution_plan_sha256", "artifact.byte_proof.native_proof");
			const auto runReceipt = StringAt(a_native, "run_receipt_sha256", "artifact.byte_proof.native_proof");
			const auto& mutants = ArrayAt(a_native, "mutants", "artifact.byte_proof.native_proof");
			RequireNumber(
				static_cast<std::int64_t>(mutants.items.size()),
				pins::kNativeMutantCount,
				"artifact.byte_proof.native_proof.mutants");

			std::vector<std::string> expectedRows;
			for (const auto family : { std::string_view("bb"), std::string_view("c36") }) {
				for (const auto& mutation : pins::kRequiredMutations) {
					if (!mutation.native)
						continue;
					expectedRows.push_back(
						std::string(family) + "|" + std::string(mutation.name) + "|" +
						std::string(mutation.bucket) + "|" + std::string(mutation.participants) + "|" +
						std::string(mutation.expectedGate) + "|" + std::string(mutation.expectedGate) + "|" +
						std::string(family) + ":" + std::string(mutation.bucket) + ":" +
						std::string(mutation.expectedGate) + ":" + std::string(mutation.semanticDelta) + "|" +
						std::string(mutation.evidenceClass));
				}
			}
			std::vector<std::string> actualRows;
			std::set<std::string> mutantHashes;
			std::set<std::string> mutantOutputs;
			std::set<std::string> mutantDetails;
			std::set<std::string> mutantSignatures;
			for (std::size_t index = 0; index < mutants.items.size(); ++index) {
				const auto label = "artifact.byte_proof.native_proof.mutants[" + std::to_string(index) + "]";
				const auto& row = mutants.items[index];
				RequireKeys(row, mutantKeys, label);
				const auto family = StringAt(row, "family", label);
				RequireTrue(family == "bb" || family == "c36", Join(label, "family"), "is unknown");
				for (const auto key : { "mutant_sha256", "reference_sha256", "neutral_output_sha256",
						 "active_output_sha256", "reference_output_sha256", "evidence_receipt_sha256" })
					DigestAt(row, key, label);
				RequireTrue(
					BooleanAt(row, "neutral_identity_holds", label),
					Join(label, "neutral_identity_holds"),
					"does not hold the neutral control");
				RequireTrue(BooleanAt(row, "rejected", label), Join(label, "rejected"), "is not rejected");
				const auto mutantSha = StringAt(row, "mutant_sha256", label);
				const auto referenceSha = StringAt(row, "reference_sha256", label);
				RequireTrue(mutantSha != referenceSha, label, "is a byte no-op");
				RequireTrue(mutantHashes.insert(std::string(mutantSha)).second, label, "aliases another mutant");
				RequireTrue(
					mutantOutputs.insert(std::string(StringAt(row, "active_output_sha256", label))).second,
					label,
					"aliases another active output");
				const auto detail = StringAt(row, "detail", label);
				RequireTrue(!detail.empty(), Join(label, "detail"), "is empty");
				RequireTrue(mutantDetails.insert(std::string(detail)).second, label, "shares a mutant detail");
				const auto signature = StringAt(row, "failure_signature", label);
				RequireTrue(
					mutantSignatures.insert(std::string(signature)).second,
					label,
					"shares a failure signature");
				const auto evidenceClass = StringAt(row, "evidence_class", label);
				RequireText(evidenceClass, kNativeStockBound, Join(label, "evidence_class"));
				++a_classCounts[std::string(evidenceClass)];
				actualRows.push_back(
					std::string(family) + "|" + std::string(StringAt(row, "mutation", label)) + "|" +
					std::string(StringAt(row, "bucket", label)) + "|" +
					CompactNode(ArrayAt(row, "participants", label)) + "|" +
					std::string(StringAt(row, "expected_gate", label)) + "|" +
					std::string(StringAt(row, "rejecting_gate", label)) + "|" + std::string(signature) + "|" +
					std::string(evidenceClass));

				std::vector<std::pair<std::string, std::string>> receiptMembers{
					{ "execution_plan_sha256", CompactString(executionPlan) },
					{ "run_receipt_sha256", CompactString(runReceipt) }
				};
				for (const auto key : { "family", "mutation", "bucket", "participants",
						 "neutral_identity_holds", "rejected", "detail", "expected_gate", "rejecting_gate",
						 "failure_signature", "mutant_sha256", "reference_sha256", "neutral_output_sha256",
						 "active_output_sha256", "reference_output_sha256", "evidence_class" })
					receiptMembers.emplace_back(key, CompactNode(Child(row, key, label)));
				RequireCommitment(
					StringAt(row, "evidence_receipt_sha256", label),
					Rcpt(CompactObject(std::move(receiptMembers))),
					Join(label, "evidence_receipt_sha256"));
			}
			RequireTrue(
				actualRows == expectedRows,
				"artifact.byte_proof.native_proof.mutants",
				"do not cover the reviewed native mutation families exactly");
			RequireNumber(
				IntegerAt(a_native, "mutants_passing_neutral_identity", "artifact.byte_proof.native_proof"),
				static_cast<std::int64_t>(mutants.items.size()),
				"artifact.byte_proof.native_proof.mutants_passing_neutral_identity");
		}

		void ValidateNativeProof(
			const Value& a_root,
			const Value& a_proof,
			std::span<const Plan> a_plans,
			OfflineInventory& a_inventory)
		{
			static constexpr std::array<std::string_view, 13> nativeKeys{
				"adapter", "checks", "evidence_class_counts", "execution_plan_sha256", "harness_build",
				"harness_source_sha256", "mechanical_pass_class", "mutants",
				"mutants_passing_neutral_identity", "passed", "plan_receipts", "run_receipt_sha256",
				"semantic_commitments"
			};
			const auto& native = ObjectAt(a_proof, "native_proof", "artifact.byte_proof");
			RequireKeys(native, nativeKeys, "artifact.byte_proof.native_proof");
			RequireTrue(
				BooleanAt(native, "passed", "artifact.byte_proof.native_proof"),
				"artifact.byte_proof.native_proof.passed",
				"is not a native PASS");
			for (const auto key : { "harness_source_sha256", "execution_plan_sha256", "run_receipt_sha256" })
				DigestAt(native, key, "artifact.byte_proof.native_proof");

			static constexpr std::array<std::string_view, 23> buildKeys{
				"binary_length", "binary_name", "binary_sha256", "build_script_sha256",
				"canonical_source_path", "canonical_temporary_directory", "canonical_working_directory",
				"compile_flags", "compiler_length", "compiler_sha256", "compiler_version",
				"environment_variables_cleared", "link_flags", "linker_length", "linker_sha256",
				"linker_version", "object_name", "receipt_sha256", "schema", "schema_version",
				"selected_build_script_object", "selected_source_object", "source_sha256"
			};
			const auto& build = ObjectAt(native, "harness_build", "artifact.byte_proof.native_proof");
			RequireKeys(build, buildKeys, "artifact.byte_proof.native_proof.harness_build");
			RequirePin(
				CompactNode(build),
				pins::kHarnessBuild,
				"artifact.byte_proof.native_proof.harness_build");
			RequireText(
				StringAt(build, "source_sha256", "artifact.byte_proof.native_proof.harness_build"),
				StringAt(native, "harness_source_sha256", "artifact.byte_proof.native_proof"),
				"artifact.byte_proof.native_proof.harness_source_sha256");
			RequireCommitment(
				StringAt(build, "receipt_sha256", "artifact.byte_proof.native_proof.harness_build"),
				RequireSha256Hex(BuildReceiptPayload(build)),
				"artifact.byte_proof.native_proof.harness_build.receipt_sha256");

			std::map<std::string, std::size_t> classCounts;
			for (const auto evidenceClass : { kNativeStockBound, std::string_view("source-recompiled-identity"),
					 kFormatRealisticSynthetic, kUnbound })
				classCounts[std::string(evidenceClass)] = 0;
			ValidateNativeChecks(native, classCounts);
			ValidateNativeMutants(native, classCounts);

			std::vector<std::pair<std::string, std::string>> expectedCounts;
			for (const auto& [name, count] : classCounts)
				expectedCounts.emplace_back(name, canonical::CompactInteger(static_cast<std::int64_t>(count)));
			const auto& counts = ObjectAt(native, "evidence_class_counts", "artifact.byte_proof.native_proof");
			RequireTrue(
				CompactNode(counts) == CompactObject(std::move(expectedCounts)),
				"artifact.byte_proof.native_proof.evidence_class_counts",
				"is not the rederived evidence tally");
			RequirePin(
				CompactNode(counts),
				pins::kEvidenceClassCounts,
				"artifact.byte_proof.native_proof.evidence_class_counts");
			RequireText(
				StringAt(native, "mechanical_pass_class", "artifact.byte_proof.native_proof"),
				kNativeStockBound,
				"artifact.byte_proof.native_proof.mechanical_pass_class");

			const auto& checks = ArrayAt(native, "checks", "artifact.byte_proof.native_proof");
			const auto& mutants = ArrayAt(native, "mutants", "artifact.byte_proof.native_proof");
			std::vector<std::string> checkSemantics;
			for (const auto& row : checks.items) {
				static constexpr std::array<std::string_view, 7> keys{
					"check", "kind", "inputs", "expected", "detail", "active_pixels", "evidence_class"
				};
				checkSemantics.push_back(canonical::CompactPick(row, keys));
			}
			std::vector<std::string> mutantSemantics;
			for (const auto& row : mutants.items) {
				static constexpr std::array<std::string_view, 16> keys{
					"family", "mutation", "bucket", "participants", "expected_gate", "rejecting_gate",
					"failure_signature", "mutant_sha256", "reference_sha256", "neutral_identity_holds",
					"rejected", "detail", "neutral_output_sha256", "active_output_sha256",
					"reference_output_sha256", "evidence_class"
				};
				mutantSemantics.push_back(canonical::CompactPick(row, keys));
			}
			const auto& planReceipts = ObjectAt(native, "plan_receipts", "artifact.byte_proof.native_proof");
			const auto& semantic = ObjectAt(native, "semantic_commitments", "artifact.byte_proof.native_proof");
			static constexpr std::array<std::string_view, 4> semanticKeys{
				"checks_sha256", "evidence_classes_sha256", "mutants_sha256", "plan_receipts_sha256"
			};
			RequireKeys(semantic, semanticKeys, "artifact.byte_proof.native_proof.semantic_commitments");
			RequireCommitment(
				StringAt(semantic, "checks_sha256", "semantic_commitments"),
				Rcpt(CompactArray(checkSemantics)),
				"artifact.byte_proof.native_proof.semantic_commitments.checks_sha256");
			RequireCommitment(
				StringAt(semantic, "mutants_sha256", "semantic_commitments"),
				Rcpt(CompactArray(mutantSemantics)),
				"artifact.byte_proof.native_proof.semantic_commitments.mutants_sha256");
			RequireCommitment(
				StringAt(semantic, "plan_receipts_sha256", "semantic_commitments"),
				Rcpt(CompactNode(planReceipts)),
				"artifact.byte_proof.native_proof.semantic_commitments.plan_receipts_sha256");
			RequireCommitment(
				StringAt(semantic, "evidence_classes_sha256", "semantic_commitments"),
				Rcpt(CompactObject({
					{ "counts", CompactNode(counts) },
					{ "mechanical_pass_class",
						CompactNode(Child(native, "mechanical_pass_class", "artifact.byte_proof.native_proof")) } })),
				"artifact.byte_proof.native_proof.semantic_commitments.evidence_classes_sha256");
			RequirePin(
				CompactNode(semantic),
				pins::kSemanticCommitments,
				"artifact.byte_proof.native_proof.semantic_commitments");

			const auto executionPlan = StringAt(native, "execution_plan_sha256", "artifact.byte_proof.native_proof");
			const auto runReceipt = StringAt(native, "run_receipt_sha256", "artifact.byte_proof.native_proof");
			RequireNumber(
				static_cast<std::int64_t>(planReceipts.members.size()),
				static_cast<std::int64_t>(a_plans.size()),
				"artifact.byte_proof.native_proof.plan_receipts");
			for (const auto& plan : a_plans) {
				const auto* receipt = planReceipts.Find(plan.planId);
				RequireTrue(
					receipt != nullptr && receipt->kind == ValueKind::kString,
					"artifact.byte_proof.native_proof.plan_receipts",
					"does not bind every plan id");
				RequireCommitment(
					receipt->text,
					Rcpt(CompactObject({
						{ "schema", CompactString("fo4re.bsdf-composite-ambient-native-plan-proof") },
						{ "schema_version", canonical::CompactInteger(2) },
						{ "plan_id", CompactString(plan.planId) },
						{ "stock_sha256", CompactString(plan.stockSha256) },
						{ "patched_sha256", CompactString(plan.patchedSha256) },
						{ "execution_plan_sha256", CompactString(executionPlan) },
						{ "run_receipt_sha256", CompactString(runReceipt) },
						{ "passed", canonical::CompactBoolean(true) } })),
					"artifact.byte_proof.native_proof.plan_receipts." + plan.planId);
				RequireText(
					receipt->text,
					plan.nativeProofReceipt,
					"artifact.patch_plans native proof receipt binding");
			}
			RequirePin(
				CompactNode(planReceipts),
				pins::kNativePlanReceipts,
				"artifact.byte_proof.native_proof.plan_receipts");

			const auto& receipts = ObjectAt(a_root, "receipts", "artifact");
			const auto nativeProofSha = StringAt(receipts, "native_proof_sha256", "artifact.receipts");
			RequireCommitment(
				nativeProofSha,
				Rcpt(CompactObject({
					{ "schema", CompactString("fo4re.bsdf-composite-ambient-native-proof") },
					{ "schema_version", canonical::CompactInteger(2) },
					{ "proof", CompactNode(native) } })),
				"artifact.receipts.native_proof_sha256");

			RequirePin(
				CompactObject({
					{ "adapter", CompactNode(Child(native, "adapter", "artifact.byte_proof.native_proof")) },
					{ "harness_source_sha256",
						CompactNode(Child(native, "harness_source_sha256", "artifact.byte_proof.native_proof")) },
					{ "harness_build", CompactNode(build) },
					{ "execution_plan_sha256", CompactString(executionPlan) },
					{ "run_receipt_sha256", CompactString(runReceipt) },
					{ "check_count", canonical::CompactInteger(static_cast<std::int64_t>(checks.items.size())) },
					{ "mutant_count", canonical::CompactInteger(static_cast<std::int64_t>(mutants.items.size())) },
					{ "evidence_class_counts", CompactNode(counts) },
					{ "mechanical_pass_class",
						CompactNode(Child(native, "mechanical_pass_class", "artifact.byte_proof.native_proof")) },
					{ "mutants_passing_neutral_identity",
						CompactNode(Child(
							native, "mutants_passing_neutral_identity", "artifact.byte_proof.native_proof")) },
					{ "semantic_commitments", CompactNode(semantic) },
					{ "checks_commitment_sha256", CompactString(Rcpt(CompactNode(checks))) },
					{ "mutants_commitment_sha256", CompactString(Rcpt(CompactNode(mutants))) },
					{ "plan_receipts", CompactNode(planReceipts) },
					{ "native_proof_sha256", CompactString(nativeProofSha) } }),
				pins::kExpectedNativeProof,
				"artifact.byte_proof.native_proof pin");

			a_inventory.nativeChecks = checks.items.size();
			a_inventory.nativeMutants = mutants.items.size();
		}

		void ValidateByteProof(
			const Value& a_root,
			std::span<const Identity> a_identities,
			const std::map<std::string, std::map<std::string, std::size_t>>& a_occurrenceTally,
			std::span<const Plan> a_plans,
			OfflineInventory& a_inventory)
		{
			static constexpr std::array<std::string_view, 11> byteProofKeys{
				"artifact_validation_mutations", "collision_counts", "composition_outcomes_by_blob",
				"composition_outcomes_by_occurrence", "evidence_policy", "native_proof", "pass_plans",
				"per_participant_set", "scope", "static_gates", "static_mutations"
			};
			const auto& proof = ObjectAt(a_root, "byte_proof", "artifact");
			RequireKeys(proof, byteProofKeys, "artifact.byte_proof");
			RequireText(
				StringAt(proof, "scope", "artifact.byte_proof"),
				"AE 1.11.221 archive mechanics and standalone byte proof only",
				"artifact.byte_proof.scope");
			RequirePin(
				CompactNode(ObjectAt(proof, "evidence_policy", "artifact.byte_proof")),
				pins::kEvidencePolicy,
				"artifact.byte_proof.evidence_policy");
			RequireNumber(
				IntegerAt(proof, "composition_outcomes_by_occurrence", "artifact.byte_proof"),
				pins::kOccurrenceOutcomes,
				"artifact.byte_proof.composition_outcomes_by_occurrence");
			RequireNumber(
				IntegerAt(proof, "composition_outcomes_by_blob", "artifact.byte_proof"),
				pins::kBlobOutcomes,
				"artifact.byte_proof.composition_outcomes_by_blob");
			RequireNumber(
				IntegerAt(proof, "pass_plans", "artifact.byte_proof"),
				static_cast<std::int64_t>(a_plans.size()),
				"artifact.byte_proof.pass_plans");
			const auto gates = TextArray(
				ArrayAt(proof, "static_gates", "artifact.byte_proof"), "artifact.byte_proof.static_gates");
			RequireTrue(
				gates.size() == pins::kStaticGates.size() &&
					std::equal(gates.begin(), gates.end(), pins::kStaticGates.begin()),
				"artifact.byte_proof.static_gates",
				"is not the reviewed gate list");

			std::vector<std::pair<std::string, std::string>> expectedTallies;
			for (std::size_t setIndex = 0; setIndex < kParticipantSetKeys.size(); ++setIndex) {
				const std::string key(kParticipantSetKeys[setIndex]);
				std::map<std::string, std::size_t> blobCounts{
					{ std::string(kVerdictFail), 0 }, { std::string(kVerdictPass), 0 },
					{ std::string(kVerdictUnproven), 0 }
				};
				for (const auto& identity : a_identities)
					++blobCounts[identity.outcomes[setIndex].verdict];
				std::map<std::string, std::size_t> occurrenceCounts{
					{ std::string(kVerdictFail), 0 }, { std::string(kVerdictPass), 0 },
					{ std::string(kVerdictUnproven), 0 }
				};
				const auto tallied = a_occurrenceTally.find(key);
				if (tallied != a_occurrenceTally.end()) {
					for (const auto& [verdict, count] : tallied->second)
						occurrenceCounts[verdict] += count;
				}
				const auto compactCounts = [](const std::map<std::string, std::size_t>& a_counts) {
					std::vector<std::pair<std::string, std::string>> members;
					for (const auto& [verdict, count] : a_counts) {
						members.emplace_back(
							verdict, canonical::CompactInteger(static_cast<std::int64_t>(count)));
					}
					return CompactObject(std::move(members));
				};
				std::size_t blobTotal = 0;
				std::size_t occurrenceTotal = 0;
				for (const auto& entry : blobCounts)
					blobTotal += entry.second;
				for (const auto& entry : occurrenceCounts)
					occurrenceTotal += entry.second;
				RequireNumber(
					static_cast<std::int64_t>(blobTotal),
					pins::kCompositeBlobs,
					"artifact.byte_proof.per_participant_set blob closure");
				RequireNumber(
					static_cast<std::int64_t>(occurrenceTotal),
					pins::kCompositeOccurrences,
					"artifact.byte_proof.per_participant_set occurrence closure");
				expectedTallies.emplace_back(
					key,
					CompactObject({ { "by_blob", compactCounts(blobCounts) },
						{ "by_occurrence", compactCounts(occurrenceCounts) } }));
			}
			const auto& tallies = ObjectAt(proof, "per_participant_set", "artifact.byte_proof");
			RequireTrue(
				CompactNode(tallies) == CompactObject(std::move(expectedTallies)),
				"artifact.byte_proof.per_participant_set",
				"is not the rederived participant tally");
			RequirePin(
				CompactNode(tallies), pins::kParticipantTallies, "artifact.byte_proof.per_participant_set");

			std::int64_t t0Blobs = 0;
			std::int64_t t0Occurrences = 0;
			std::int64_t t13Blobs = 0;
			std::int64_t t13Occurrences = 0;
			for (const auto& identity : a_identities) {
				const auto occurrences = static_cast<std::int64_t>(identity.occurrences);
				for (const auto& declaration : identity.declarations) {
					if (declaration.reg == 0) {
						++t0Blobs;
						t0Occurrences += occurrences;
					}
					if (declaration.reg == 13) {
						++t13Blobs;
						t13Occurrences += occurrences;
					}
				}
			}
			const auto& collisions = ObjectAt(proof, "collision_counts", "artifact.byte_proof");
			RequireTrue(
				CompactNode(collisions) ==
					CompactObject({
						{ "t0_blobs", canonical::CompactInteger(t0Blobs) },
						{ "t0_occurrences", canonical::CompactInteger(t0Occurrences) },
						{ "t13_blobs", canonical::CompactInteger(t13Blobs) },
						{ "t13_occurrences", canonical::CompactInteger(t13Occurrences) },
						{ "SSGI_fail_blobs", canonical::CompactInteger(t0Blobs) },
						{ "SSGI_fail_occurrences", canonical::CompactInteger(t0Occurrences) },
						{ "SSGI+Wetness_fail_blobs", canonical::CompactInteger(t0Blobs) },
						{ "SSGI+Wetness_fail_occurrences", canonical::CompactInteger(t0Occurrences) } }),
				"artifact.byte_proof.collision_counts",
				"is not the rederived collision tally");
			RequirePin(
				CompactNode(collisions), pins::kCollisionCounts, "artifact.byte_proof.collision_counts");

			static constexpr std::array<std::string_view, 15> staticMutationKeys{
				"baseline_executable_sha256", "baseline_sha256", "bucket", "cause_scope", "detail",
				"evidence_class", "evidence_receipt_sha256", "expected_gate", "failure_signature",
				"family", "mutant_executable_sha256", "mutant_sha256", "mutation", "rejected",
				"rejecting_gate"
			};
			static constexpr std::array<std::string_view, 1> receiptKey{ "evidence_receipt_sha256" };
			const auto& staticMutations = ArrayAt(proof, "static_mutations", "artifact.byte_proof");
			RequireNumber(
				static_cast<std::int64_t>(staticMutations.items.size()),
				pins::kStaticMutationCount,
				"artifact.byte_proof.static_mutations");
			std::vector<std::string> expectedStatic;
			for (const auto family : { std::string_view("bb"), std::string_view("c36") }) {
				for (const auto& mutation : pins::kRequiredMutations) {
					if (mutation.native || mutation.participants == "[]")
						continue;
					expectedStatic.push_back(
						std::string(family) + "|" + std::string(mutation.name) + "|" +
						std::string(mutation.bucket) + "|" + std::string(mutation.causeScope) + "|" +
						std::string(mutation.expectedGate) + "|" + std::string(mutation.expectedGate) + "|" +
						std::string(family) + ":" + std::string(mutation.bucket) + ":" +
						std::string(mutation.expectedGate) + ":" + std::string(mutation.semanticDelta) + "|" +
						std::string(mutation.evidenceClass));
				}
			}
			std::vector<std::string> actualStatic;
			std::map<std::string, std::set<std::string>> mutantHashes;
			std::map<std::string, std::set<std::string>> executableHashes;
			std::map<std::string, std::set<std::string>> details;
			std::map<std::string, std::set<std::string>> signatures;
			for (std::size_t index = 0; index < staticMutations.items.size(); ++index) {
				const auto label = "artifact.byte_proof.static_mutations[" + std::to_string(index) + "]";
				const auto& row = staticMutations.items[index];
				RequireKeys(row, staticMutationKeys, label);
				const auto family = StringAt(row, "family", label);
				RequireTrue(family == "bb" || family == "c36", Join(label, "family"), "is unknown");
				for (const auto key : { "baseline_sha256", "mutant_sha256", "baseline_executable_sha256",
						 "mutant_executable_sha256", "evidence_receipt_sha256" })
					DigestAt(row, key, label);
				const auto baseline = StringAt(row, "baseline_sha256", label);
				const auto mutant = StringAt(row, "mutant_sha256", label);
				RequireTrue(baseline != mutant, label, "is a byte no-op");
				RequireTrue(
					mutantHashes[std::string(family)].insert(std::string(mutant)).second,
					label,
					"aliases another mutant");
				const auto causeScope = StringAt(row, "cause_scope", label);
				const auto baselineExecutable = StringAt(row, "baseline_executable_sha256", label);
				const auto mutantExecutable = StringAt(row, "mutant_executable_sha256", label);
				if (causeScope == "executable") {
					RequireTrue(baselineExecutable != mutantExecutable, label, "has no ISA delta");
					RequireTrue(
						executableHashes[std::string(family)].insert(std::string(mutantExecutable)).second,
						label,
						"aliases another ISA mutant");
				} else if (causeScope == "container") {
					RequireText(mutantExecutable, baselineExecutable, Join(label, "mutant_executable_sha256"));
				} else {
					Violate(Join(label, "cause_scope"), "is not a reviewed cause scope");
				}
				RequireTrue(BooleanAt(row, "rejected", label), Join(label, "rejected"), "is not rejected");
				const auto detail = StringAt(row, "detail", label);
				RequireTrue(!detail.empty(), Join(label, "detail"), "is empty");
				RequireTrue(
					details[std::string(family)].insert(std::string(detail)).second,
					label,
					"shares a mutation detail");
				const auto signature = StringAt(row, "failure_signature", label);
				RequireTrue(
					signatures[std::string(family)].insert(std::string(signature)).second,
					label,
					"shares a failure signature");
				RequireCommitment(
					StringAt(row, "evidence_receipt_sha256", label),
					Rcpt(canonical::CompactOmit(row, receiptKey)),
					Join(label, "evidence_receipt_sha256"));
				actualStatic.push_back(
					std::string(family) + "|" + std::string(StringAt(row, "mutation", label)) + "|" +
					std::string(StringAt(row, "bucket", label)) + "|" + std::string(causeScope) + "|" +
					std::string(StringAt(row, "expected_gate", label)) + "|" +
					std::string(StringAt(row, "rejecting_gate", label)) + "|" + std::string(signature) + "|" +
					std::string(StringAt(row, "evidence_class", label)));
			}
			RequireTrue(
				actualStatic == expectedStatic,
				"artifact.byte_proof.static_mutations",
				"do not cover the reviewed static mutation families exactly");

			const auto& artifactMutations = ArrayAt(proof, "artifact_validation_mutations", "artifact.byte_proof");
			RequireNumber(
				static_cast<std::int64_t>(artifactMutations.items.size()),
				pins::kArtifactMutationCount,
				"artifact.byte_proof.artifact_validation_mutations");
			static constexpr std::array<std::string_view, 3> artifactMutationKeys{
				"evidence_class", "expected_gate", "mutation"
			};
			std::size_t artifactIndex = 0;
			for (const auto& mutation : pins::kRequiredMutations) {
				if (mutation.participants != "[]")
					continue;
				const auto label =
					"artifact.byte_proof.artifact_validation_mutations[" + std::to_string(artifactIndex) + "]";
				RequireTrue(
					artifactIndex < artifactMutations.items.size(),
					"artifact.byte_proof.artifact_validation_mutations",
					"does not cover every reviewed artifact mutation");
				const auto& row = artifactMutations.items[artifactIndex];
				RequireKeys(row, artifactMutationKeys, label);
				RequireText(StringAt(row, "mutation", label), mutation.name, Join(label, "mutation"));
				RequireText(StringAt(row, "expected_gate", label), mutation.expectedGate, Join(label, "expected_gate"));
				RequireText(
					StringAt(row, "evidence_class", label), mutation.evidenceClass, Join(label, "evidence_class"));
				++artifactIndex;
			}
			RequireNumber(
				static_cast<std::int64_t>(artifactIndex),
				static_cast<std::int64_t>(artifactMutations.items.size()),
				"artifact.byte_proof.artifact_validation_mutations");
			RequirePin(
				CompactNode(artifactMutations),
				pins::kArtifactValidationMutations,
				"artifact.byte_proof.artifact_validation_mutations");

			ValidateNativeProof(a_root, proof, a_plans, a_inventory);

			a_inventory.staticMutations = staticMutations.items.size();
			a_inventory.staticGates = gates.size();
			a_inventory.artifactMutations = artifactMutations.items.size();
		}
	}

	ValidatedArtifact ValidateArtifact(const canonical::Value& a_root)
	{
		ValidatedArtifact result;
		auto& model = result.model;
		auto& inventory = result.inventory;

		RequireKeys(a_root, kRootKeys, "artifact");
		model.schema = StringAt(a_root, "schema", "artifact");
		model.schemaVersion = IntegerAt(a_root, "schema_version", "artifact");
		RequireText(model.schema, pins::kSchema, "artifact.schema");
		RequireNumber(model.schemaVersion, pins::kSchemaVersion, "artifact.schema_version");

		RequireCensus(
			canonical::ScalarKindCensus(a_root, ValueKind::kDouble),
			pins::kDoubleCensus,
			"artifact double census");
		RequireCensus(
			canonical::ScalarKindCensus(a_root, ValueKind::kNull),
			pins::kNullCensus,
			"artifact null census");
		RequireCensus(
			canonical::ScalarKindCensus(a_root, ValueKind::kBoolean),
			pins::kBooleanCensus,
			"artifact boolean census");

		const auto& engineScope = ObjectAt(a_root, "engine_scope", "artifact");
		RequirePin(CompactNode(engineScope), pins::kEngineScope, "artifact.engine_scope");
		const auto& archive = ObjectAt(a_root, "archive", "artifact");
		RequirePin(CompactNode(archive), pins::kArchive, "artifact.archive");
		const auto& census = ObjectAt(a_root, "census", "artifact");
		RequirePin(CompactNode(census), pins::kCensus, "artifact.census");
		RequireNumber(
			IntegerAt(census, "structural_count", "artifact.census"),
			pins::kStructuralCount,
			"artifact.census.structural_count");
		RequireNumber(
			IntegerAt(census, "byte_scan_count", "artifact.census"),
			pins::kByteScanCount,
			"artifact.census.byte_scan_count");
		RequireNumber(
			IntegerAt(census, "unique_shader_blobs", "artifact.census"),
			pins::kUniqueShaderBlobs,
			"artifact.census.unique_shader_blobs");
		const auto& participants = ObjectAt(a_root, "participants", "artifact");
		RequirePin(CompactNode(participants), pins::kParticipants, "artifact.participants");
		const auto& denominator = ObjectAt(a_root, "denominator", "artifact");
		RequirePin(CompactNode(denominator), pins::kDenominator, "artifact.denominator");
		const auto& fallbackGraph = ObjectAt(a_root, "fallback_graph", "artifact");
		RequirePin(CompactNode(fallbackGraph), pins::kFallbackGraph, "artifact.fallback_graph");
		const auto& releasePolicy = ObjectAt(a_root, "release_policy", "artifact");
		RequirePin(CompactNode(releasePolicy), pins::kReleasePolicy, "artifact.release_policy");
		const auto& routeAdmission = ObjectAt(a_root, "route_admission", "artifact");
		RequirePin(CompactNode(routeAdmission), pins::kRouteAdmission, "artifact.route_admission");

		const auto& classification = ObjectAt(a_root, "classification", "artifact");
		static constexpr std::array<std::string_view, 13> classificationKeys{
			"fxp_ordinal_first", "fxp_ordinal_last", "joins", "key_domain", "key_domain_note",
			"normalizer_diagnostic", "occurrence_count", "profile", "runtime_observations",
			"scoped_occurrence_commitment_sha256", "stage", "subclass", "unique_blob_count"
		};
		RequireKeys(classification, classificationKeys, "artifact.classification");
		static constexpr std::array<std::string_view, 1> normalizerKey{ "normalizer_diagnostic" };
		RequirePin(
			canonical::CompactOmit(classification, normalizerKey),
			pins::kClassificationScalars,
			"artifact.classification");
		const auto& normalizer = ArrayAt(classification, "normalizer_diagnostic", "artifact.classification");
		RequireNumber(
			static_cast<std::int64_t>(normalizer.items.size()),
			pins::kNormalizerRowCount,
			"artifact.classification.normalizer_diagnostic");
		static constexpr std::array<std::string_view, 4> normalizerRowKeys{
			"formula_output", "fxp_key", "fxp_ordinal", "stock_sha256"
		};
		for (std::size_t index = 0; index < normalizer.items.size(); ++index) {
			const auto label = "artifact.classification.normalizer_diagnostic[" + std::to_string(index) + "]";
			const auto& row = normalizer.items[index];
			RequireKeys(row, normalizerRowKeys, label);
			const auto& pin = pins::kNormalizerDiagnostic[index];
			RequirePin(StringAt(row, "formula_output", label), pin.formulaOutput, label);
			RequirePin(StringAt(row, "fxp_key", label), pin.fxpKey, label);
			RequirePin(
				std::to_string(IntegerAt(row, "fxp_ordinal", label)),
				std::to_string(pin.fxpOrdinal),
				label);
			RequirePin(StringAt(row, "stock_sha256", label), pin.stockSha256, label);
		}

		// Patch plans first: identity outcomes are rederived from the plan matrix.
		const auto& planArray = ArrayAt(a_root, "patch_plans", "artifact");
		RequireNumber(
			static_cast<std::int64_t>(planArray.items.size()),
			pins::kPassPlans,
			"artifact.patch_plans");
		static constexpr std::array<std::string_view, 24> planKeys{
			"added_resource_claims", "edits", "fallback", "fallback_proven",
			"mechanical_evidence_class", "native_proof_receipt_sha256", "participants", "patched",
			"patched_dcl_temps", "patched_declaration_commitment_sha256",
			"patched_resource_declarations", "plan_id", "proof_status", "receipts", "recipe_id",
			"scratch_components", "static_proof", "stock_dcl_temps",
			"stock_declaration_commitment_sha256", "stock_length", "stock_resource_declarations",
			"stock_sha256", "target", "target_status"
		};
		static_assert(
			planKeys.size() == static_cast<std::size_t>(pins::kPatchPlanKeyCount),
			"the reviewed patch plan key count is exactly 24");
		std::vector<Plan> plans;
		std::map<std::string, const Plan*> planLookup;
		std::vector<std::string> planReceipts;
		std::vector<std::string> staticProofReceipts;
		std::map<std::string, std::set<std::string>> setsByStock;
		plans.reserve(planArray.items.size());
		for (std::size_t index = 0; index < planArray.items.size(); ++index) {
			const auto label = "artifact.patch_plans[" + std::to_string(index) + "]";
			const auto& row = planArray.items[index];
			RequireKeys(row, planKeys, label);

			Plan plan;
			plan.planId = StringAt(row, "plan_id", label);
			plan.target = StringAt(row, "target", label);
			plan.stockSha256 = DigestAt(row, "stock_sha256", label);
			plan.stockLength = IntegerAt(row, "stock_length", label);
			plan.participants = TextArray(
				ArrayAt(row, "participants", label), Join(label, "participants"));
			RequireCanonicalParticipants(plan.participants, Join(label, "participants"));
			plan.participantKey = ParticipantKey(plan.participants);
			plan.proofStatus = StringAt(row, "proof_status", label);
			RequireText(StringAt(row, "target_status", label), kVerdictUnproven, Join(label, "target_status"));
			RequireText(plan.proofStatus, kVerdictPass, Join(label, "proof_status"));
			RequireText(
				StringAt(row, "mechanical_evidence_class", label),
				kNativeStockBound,
				Join(label, "mechanical_evidence_class"));

			const auto& fallback = ArrayAt(row, "fallback", label);
			RequireTrue(fallback.items.empty(), Join(label, "fallback"), "is not empty");
			RequireTrue(
				!BooleanAt(row, "fallback_proven", label),
				Join(label, "fallback_proven"),
				"claims a proven fallback");

			plan.stockDeclarations = ValidateDeclarations(
				ArrayAt(row, "stock_resource_declarations", label),
				Join(label, "stock_resource_declarations"));
			const auto patchedDeclarations = ValidateDeclarations(
				ArrayAt(row, "patched_resource_declarations", label),
				Join(label, "patched_resource_declarations"));
			RequireCommitment(
				StringAt(row, "stock_declaration_commitment_sha256", label),
				Rcpt(CompactNode(ArrayAt(row, "stock_resource_declarations", label))),
				Join(label, "stock_declaration_commitment_sha256"));
			RequireCommitment(
				StringAt(row, "patched_declaration_commitment_sha256", label),
				Rcpt(CompactNode(ArrayAt(row, "patched_resource_declarations", label))),
				Join(label, "patched_declaration_commitment_sha256"));

			const auto& claims = ArrayAt(row, "added_resource_claims", label);
			RequireNumber(
				static_cast<std::int64_t>(claims.items.size()),
				static_cast<std::int64_t>(plan.participants.size()),
				Join(label, "added_resource_claims"));
			static constexpr std::array<std::string_view, 5> claimKeys{
				"dimension", "participant", "register", "resource_type", "return_token"
			};
			std::vector<DeclarationRow> expectedPatched = plan.stockDeclarations;
			for (std::size_t claimIndex = 0; claimIndex < claims.items.size(); ++claimIndex) {
				const auto claimLabel = label + ".added_resource_claims[" + std::to_string(claimIndex) + "]";
				const auto& claim = claims.items[claimIndex];
				RequireKeys(claim, claimKeys, claimLabel);
				const auto participant = StringAt(claim, "participant", claimLabel);
				RequireText(participant, plan.participants[claimIndex], Join(claimLabel, "participant"));
				const bool ssgi = participant == "SSGI";
				RequireNumber(
					IntegerAt(claim, "register", claimLabel),
					ssgi ? 0 : 13,
					Join(claimLabel, "register"));
				RequireNumber(IntegerAt(claim, "dimension", claimLabel), 3, Join(claimLabel, "dimension"));
				RequireText(
					StringAt(claim, "resource_type", claimLabel),
					ssgi ? "Texture2D<float4>" : "Texture2D<float>",
					Join(claimLabel, "resource_type"));
				RequireText(
					StringAt(claim, "return_token", claimLabel),
					"0x00005555",
					Join(claimLabel, "return_token"));
				expectedPatched.push_back(DeclarationRow{ ssgi ? 0 : 13, 3, "0x00005555" });
			}
			std::sort(
				expectedPatched.begin(),
				expectedPatched.end(),
				[](const DeclarationRow& a_left, const DeclarationRow& a_right) {
					return a_left.reg < a_right.reg;
				});
			RequireTrue(
				expectedPatched.size() == patchedDeclarations.size() &&
					std::equal(
						expectedPatched.begin(),
						expectedPatched.end(),
						patchedDeclarations.begin(),
						[](const DeclarationRow& a_left, const DeclarationRow& a_right) {
							return a_left.reg == a_right.reg &&
							       a_left.dimension == a_right.dimension &&
							       a_left.returnToken == a_right.returnToken;
						}),
				Join(label, "patched_resource_declarations"),
				"is not stock plus exactly the requested slots");

			const auto& edits = ArrayAt(row, "edits", label);
			RequireTrue(!edits.items.empty(), Join(label, "edits"), "is empty");
			static constexpr std::array<std::string_view, 9> editKeys{
				"executable_dword", "file_offset", "kind", "order", "participant", "preimage",
				"replaced_dwords", "role", "tokens"
			};
			static constexpr std::array<std::string_view, 3> preimageKeys{ "length", "offset", "sha256" };
			std::int64_t previousOrder = -1;
			std::int64_t previousDword = -1;
			std::int64_t previousEnd = -1;
			std::int64_t previousFileOffset = -1;
			for (std::size_t editIndex = 0; editIndex < edits.items.size(); ++editIndex) {
				const auto editLabel = label + ".edits[" + std::to_string(editIndex) + "]";
				const auto& edit = edits.items[editIndex];
				RequireKeys(edit, editKeys, editLabel);
				const auto kind = StringAt(edit, "kind", editLabel);
				RequireTrue(kind == "insert" || kind == "replace", Join(editLabel, "kind"), "is unknown");
				const auto participant = StringAt(edit, "participant", editLabel);
				RequireTrue(
					participant == "SSGI" || participant == "Wetness" || participant == "shared",
					Join(editLabel, "participant"),
					"is unknown");
				RequireTrue(
					!StringAt(edit, "role", editLabel).empty(),
					Join(editLabel, "role"),
					"is empty");
				const auto order = IntegerAt(edit, "order", editLabel);
				RequireTrue(order > previousOrder, Join(editLabel, "order"), "is not strictly ascending");
				previousOrder = order;
				const auto executableDword = IntegerAt(edit, "executable_dword", editLabel);
				const auto fileOffset = IntegerAt(edit, "file_offset", editLabel);
				RequireTrue(executableDword >= 0, Join(editLabel, "executable_dword"), "is negative");
				RequireTrue(fileOffset >= 0, Join(editLabel, "file_offset"), "is negative");
				const auto& replaced = ArrayAt(edit, "replaced_dwords", editLabel);
				const auto& tokens = ArrayAt(edit, "tokens", editLabel);
				RequireTrue(!tokens.items.empty(), Join(editLabel, "tokens"), "is empty");
				for (const auto& token : tokens.items) {
					RequireTrue(
						token.kind == ValueKind::kString && IsLowerHex(token.text, 8),
						Join(editLabel, "tokens"),
						"carries a malformed dword");
				}
				for (const auto& token : replaced.items) {
					RequireTrue(
						token.kind == ValueKind::kString && IsLowerHex(token.text, 8),
						Join(editLabel, "replaced_dwords"),
						"carries a malformed dword");
				}
				if (kind == "insert") {
					RequireTrue(
						replaced.items.empty(),
						Join(editLabel, "replaced_dwords"),
						"is not empty for an insertion");
				} else {
					RequireTrue(
						!replaced.items.empty(),
						Join(editLabel, "replaced_dwords"),
						"is empty for a replacement");
				}
				RequireTrue(
					executableDword > previousDword,
					Join(editLabel, "executable_dword"),
					"is not strictly ascending");
				RequireTrue(
					fileOffset > previousFileOffset,
					Join(editLabel, "file_offset"),
					"is not strictly ascending");
				RequireTrue(
					executableDword >= previousEnd,
					Join(editLabel, "executable_dword"),
					"overlaps the previous edit");
				previousDword = executableDword;
				previousFileOffset = fileOffset;
				previousEnd = executableDword + static_cast<std::int64_t>(replaced.items.size());
				const auto& preimage = ObjectAt(edit, "preimage", editLabel);
				RequireKeys(preimage, preimageKeys, Join(editLabel, "preimage"));
				const auto preimageOffset = IntegerAt(preimage, "offset", Join(editLabel, "preimage"));
				const auto preimageLength = IntegerAt(preimage, "length", Join(editLabel, "preimage"));
				DigestAt(preimage, "sha256", Join(editLabel, "preimage"));
				RequireTrue(preimageLength > 0, Join(editLabel, "preimage.length"), "is zero");
				RequireTrue(
					preimageOffset <= fileOffset && fileOffset < preimageOffset + preimageLength,
					Join(editLabel, "preimage"),
					"does not cover its edit site");
			}

			const auto& patched = ObjectAt(row, "patched", label);
			static constexpr std::array<std::string_view, 4> patchedKeys{
				"checksum", "length", "sha1", "sha256"
			};
			RequireKeys(patched, patchedKeys, Join(label, "patched"));
			DigestAt(patched, "checksum", Join(label, "patched"), 32);
			DigestAt(patched, "sha1", Join(label, "patched"), 40);
			const auto patchedSha256 = DigestAt(patched, "sha256", Join(label, "patched"));
			RequireTrue(
				IntegerAt(patched, "length", Join(label, "patched")) > plan.stockLength,
				Join(label, "patched.length"),
				"does not grow the stock blob");

			const auto& staticProof = ObjectAt(row, "static_proof", label);
			static constexpr std::array<std::string_view, 14> staticKeys{
				"added_resource_registers", "gates", "guarded_stock_write_dwords", "ordering",
				"output_masks", "participants", "passed", "patched_dcl_temps",
				"patched_maximum_temp_register", "patched_resource_registers", "scratch_components",
				"stock_dcl_temps", "stock_maximum_temp_register", "stock_resource_registers"
			};
			RequireKeys(staticProof, staticKeys, Join(label, "static_proof"));
			RequireTrue(
				BooleanAt(staticProof, "passed", Join(label, "static_proof")),
				Join(label, "static_proof.passed"),
				"is not a static PASS");
			const auto gates = TextArray(
				ArrayAt(staticProof, "gates", Join(label, "static_proof")),
				Join(label, "static_proof.gates"));
			RequireTrue(
				gates.size() == pins::kStaticGates.size() &&
					std::equal(gates.begin(), gates.end(), pins::kStaticGates.begin()),
				Join(label, "static_proof.gates"),
				"is not the reviewed gate list");
			const auto outputMasks = TextArray(
				ArrayAt(staticProof, "output_masks", Join(label, "static_proof")),
				Join(label, "static_proof.output_masks"));
			RequireTrue(
				outputMasks.size() == 3 && outputMasks[0] == "xyz" && outputMasks[1] == "w" &&
					outputMasks[2] == "xyzw",
				Join(label, "static_proof.output_masks"),
				"is not the reviewed output mask set");
			const auto staticParticipants = TextArray(
				ArrayAt(staticProof, "participants", Join(label, "static_proof")),
				Join(label, "static_proof.participants"));
			RequireTrue(
				staticParticipants == plan.participants,
				Join(label, "static_proof.participants"),
				"does not match the plan participant set");
			const auto ordering = TextArray(
				ArrayAt(staticProof, "ordering", Join(label, "static_proof")),
				Join(label, "static_proof.ordering"));
			std::vector<std::string> expectedOrdering;
			const bool hasWetness = plan.participantKey.find("Wetness") != std::string::npos;
			const bool hasSsgi = plan.participantKey.find("SSGI") != std::string::npos;
			if (hasWetness)
				expectedOrdering.emplace_back(kOrderingCatalog[0]);
			if (hasSsgi)
				expectedOrdering.emplace_back(kOrderingCatalog[1]);
			if (hasWetness && hasSsgi)
				expectedOrdering.emplace_back(kOrderingCatalog[2]);
			RequireTrue(
				ordering == expectedOrdering,
				Join(label, "static_proof.ordering"),
				"is not the reviewed ordering subset");

			const auto& guarded = ObjectAt(staticProof, "guarded_stock_write_dwords", Join(label, "static_proof"));
			auto guardedKeys = guarded.Keys();
			std::vector<std::string_view> expectedGuarded;
			for (const auto& participant : plan.participants)
				expectedGuarded.emplace_back(participant);
			std::sort(expectedGuarded.begin(), expectedGuarded.end());
			RequireTrue(
				guardedKeys.size() == expectedGuarded.size() &&
					std::equal(guardedKeys.begin(), guardedKeys.end(), expectedGuarded.begin()),
				Join(label, "static_proof.guarded_stock_write_dwords"),
				"does not carry exactly the plan participants");

			const auto stockTemps = IntegerAt(row, "stock_dcl_temps", label);
			const auto patchedTemps = IntegerAt(row, "patched_dcl_temps", label);
			RequireNumber(
				IntegerAt(staticProof, "stock_dcl_temps", Join(label, "static_proof")),
				stockTemps,
				Join(label, "static_proof.stock_dcl_temps"));
			RequireNumber(
				IntegerAt(staticProof, "patched_dcl_temps", Join(label, "static_proof")),
				patchedTemps,
				Join(label, "static_proof.patched_dcl_temps"));
			RequireTrue(patchedTemps > stockTemps, Join(label, "patched_dcl_temps"), "does not add scratch");
			RequireTrue(
				IntegerAt(staticProof, "stock_maximum_temp_register", Join(label, "static_proof")) < stockTemps,
				Join(label, "static_proof.stock_maximum_temp_register"),
				"is not below the stock temporary count");
			RequireTrue(
				IntegerAt(staticProof, "patched_maximum_temp_register", Join(label, "static_proof")) <
					patchedTemps,
				Join(label, "static_proof.patched_maximum_temp_register"),
				"is not below the patched temporary count");

			const auto registersOf = [](const std::vector<DeclarationRow>& a_rows) {
				std::vector<std::int64_t> registers;
				registers.reserve(a_rows.size());
				for (const auto& declaration : a_rows)
					registers.push_back(declaration.reg);
				return registers;
			};
			const auto integerArray = [](const Value& a_array, std::string_view a_label) {
				std::vector<std::int64_t> values;
				values.reserve(a_array.items.size());
				for (const auto& item : a_array.items)
					values.push_back(Typed(item, ValueKind::kInteger, a_label).integer);
				return values;
			};
			RequireTrue(
				integerArray(
					ArrayAt(staticProof, "stock_resource_registers", Join(label, "static_proof")),
					Join(label, "static_proof.stock_resource_registers")) ==
					registersOf(plan.stockDeclarations),
				Join(label, "static_proof.stock_resource_registers"),
				"does not match the stock declarations");
			RequireTrue(
				integerArray(
					ArrayAt(staticProof, "patched_resource_registers", Join(label, "static_proof")),
					Join(label, "static_proof.patched_resource_registers")) ==
					registersOf(patchedDeclarations),
				Join(label, "static_proof.patched_resource_registers"),
				"does not match the patched declarations");
			std::vector<std::int64_t> expectedAdded;
			for (const auto& participant : plan.participants)
				expectedAdded.push_back(participant == "SSGI" ? 0 : 13);
			std::sort(expectedAdded.begin(), expectedAdded.end());
			RequireTrue(
				integerArray(
					ArrayAt(staticProof, "added_resource_registers", Join(label, "static_proof")),
					Join(label, "static_proof.added_resource_registers")) == expectedAdded,
				Join(label, "static_proof.added_resource_registers"),
				"does not match the requested slots");

			const auto& scratch = ArrayAt(row, "scratch_components", label);
			RequireTrue(
				CompactNode(scratch) ==
					CompactNode(ArrayAt(staticProof, "scratch_components", Join(label, "static_proof"))),
				Join(label, "scratch_components"),
				"does not match the static proof scratch rows");
			static constexpr std::array<std::string_view, 6> scratchKeys{
				"component", "first_write_patched_dword", "last_read_patched_dword", "participant",
				"register", "role"
			};
			for (std::size_t scratchIndex = 0; scratchIndex < scratch.items.size(); ++scratchIndex) {
				const auto scratchLabel = label + ".scratch_components[" + std::to_string(scratchIndex) + "]";
				const auto& component = scratch.items[scratchIndex];
				RequireKeys(component, scratchKeys, scratchLabel);
				const auto channel = StringAt(component, "component", scratchLabel);
				RequireTrue(
					channel == "x" || channel == "y" || channel == "z" || channel == "w",
					Join(scratchLabel, "component"),
					"is not a register channel");
				const auto owner = StringAt(component, "participant", scratchLabel);
				RequireTrue(
					owner == "SSGI" || owner == "Wetness",
					Join(scratchLabel, "participant"),
					"is not a participant");
				RequireText(
					StringAt(component, "role", scratchLabel),
					"feature-scratch",
					Join(scratchLabel, "role"));
				const auto reg = IntegerAt(component, "register", scratchLabel);
				RequireTrue(
					reg >= stockTemps && reg < patchedTemps,
					Join(scratchLabel, "register"),
					"is not inside the fresh scratch range");
				RequireTrue(
					IntegerAt(component, "first_write_patched_dword", scratchLabel) <=
						IntegerAt(component, "last_read_patched_dword", scratchLabel),
					scratchLabel,
					"reads its scratch channel before the first write");
			}

			const auto& receipts = ObjectAt(row, "receipts", label);
			static constexpr std::array<std::string_view, 2> receiptKeys{
				"plan_sha256", "static_proof_sha256"
			};
			RequireKeys(receipts, receiptKeys, Join(label, "receipts"));
			const auto planReceipt = DigestAt(receipts, "plan_sha256", Join(label, "receipts"));
			const auto staticReceipt = DigestAt(
				receipts, "static_proof_sha256", Join(label, "receipts"));
			const auto nativeReceipt = DigestAt(row, "native_proof_receipt_sha256", label);

			const auto stockDeclarationCommitment = Rcpt(
				CompactNode(ArrayAt(row, "stock_resource_declarations", label)));
			const auto patchedDeclarationCommitment = Rcpt(
				CompactNode(ArrayAt(row, "patched_resource_declarations", label)));
			const auto editCommitment = Rcpt(CompactNode(edits));
			RequireCommitment(
				planReceipt,
				Rcpt(CompactObject({
					{ "schema", CompactString("fo4re.bsdf-composite-ambient-plan-receipt") },
					{ "schema_version", canonical::CompactInteger(2) },
					{ "plan_id", CompactString(plan.planId) },
					{ "recipe_id", CompactNode(Child(row, "recipe_id", label)) },
					{ "target", CompactString(plan.target) },
					{ "participants", CompactNode(ArrayAt(row, "participants", label)) },
					{ "stock_sha256", CompactString(plan.stockSha256) },
					{ "stock_length", canonical::CompactInteger(plan.stockLength) },
					{ "patched", CompactNode(patched) },
					{ "stock_dcl_temps", canonical::CompactInteger(stockTemps) },
					{ "patched_dcl_temps", canonical::CompactInteger(patchedTemps) },
					{ "stock_declarations_sha256", CompactString(stockDeclarationCommitment) },
					{ "patched_declarations_sha256", CompactString(patchedDeclarationCommitment) },
					{ "edits_sha256", CompactString(editCommitment) } })),
				Join(label, "receipts.plan_sha256"));
			RequireCommitment(
				staticReceipt,
				Rcpt(CompactObject({
					{ "schema", CompactString("fo4re.bsdf-composite-ambient-static-proof") },
					{ "schema_version", canonical::CompactInteger(2) },
					{ "plan_id", CompactString(plan.planId) },
					{ "participants", CompactNode(ArrayAt(row, "participants", label)) },
					{ "stock_sha256", CompactString(plan.stockSha256) },
					{ "patched_sha256", CompactString(patchedSha256) },
					{ "proof", CompactNode(staticProof) } })),
				Join(label, "receipts.static_proof_sha256"));

			const auto& pin = pins::kExpectedPlans[index];
			RequirePin(plan.planId, pin.planId, label);
			RequirePin(StringAt(row, "recipe_id", label), pin.recipeId, Join(label, "recipe_id"));
			RequirePin(plan.target, pin.target, Join(label, "target"));
			RequirePin(plan.stockSha256, pin.stockSha256, Join(label, "stock_sha256"));
			RequirePin(
				CompactNode(ArrayAt(row, "participants", label)),
				pin.participants,
				Join(label, "participants"));
			RequirePin(plan.proofStatus, pin.proofStatus, Join(label, "proof_status"));
			RequirePin(
				StringAt(row, "mechanical_evidence_class", label),
				pin.mechanicalEvidenceClass,
				Join(label, "mechanical_evidence_class"));
			RequirePin(
				stockDeclarationCommitment,
				pin.stockDeclarationCommitment,
				Join(label, "stock_declaration_commitment_sha256"));
			RequirePin(
				patchedDeclarationCommitment,
				pin.patchedDeclarationCommitment,
				Join(label, "patched_declaration_commitment_sha256"));
			RequirePin(CompactNode(patched), pin.patched, Join(label, "patched"));
			RequirePin(editCommitment, pin.editCommitment, Join(label, "edits"));
			RequirePin(
				Rcpt(CompactNode(staticProof)),
				pin.staticProofCommitment,
				Join(label, "static_proof"));
			RequirePin(
				Rcpt(CompactNode(claims)),
				pin.addedResourceClaimsCommitment,
				Join(label, "added_resource_claims"));
			RequirePin(
				Rcpt(CompactNode(scratch)),
				pin.scratchComponentsCommitment,
				Join(label, "scratch_components"));
			RequirePin(planReceipt, pin.planReceipt, Join(label, "receipts.plan_sha256"));
			RequirePin(staticReceipt, pin.staticProofReceipt, Join(label, "receipts.static_proof_sha256"));
			RequirePin(nativeReceipt, pin.nativeProofReceipt, Join(label, "native_proof_receipt_sha256"));
			RequirePin(std::to_string(plan.stockLength), std::to_string(pin.stockLength), Join(label, "stock_length"));
			RequirePin(std::to_string(stockTemps), std::to_string(pin.stockDclTemps), Join(label, "stock_dcl_temps"));
			RequirePin(
				std::to_string(patchedTemps),
				std::to_string(pin.patchedDclTemps),
				Join(label, "patched_dcl_temps"));

			PlanRecord record;
			record.planId = plan.planId;
			record.recipeId = StringAt(row, "recipe_id", label);
			record.target = plan.target;
			record.stockSha256 = plan.stockSha256;
			record.participants = plan.participants;
			record.proofStatus = plan.proofStatus;
			record.mechanicalEvidenceClass = StringAt(row, "mechanical_evidence_class", label);
			record.stockLength = plan.stockLength;
			record.stockDclTemps = stockTemps;
			record.patchedDclTemps = patchedTemps;
			record.patchedSha256 = patchedSha256;
			record.planReceiptSha256 = planReceipt;
			record.staticProofReceiptSha256 = staticReceipt;
			record.nativeProofReceiptSha256 = nativeReceipt;
			record.edits = edits.items.size();
			record.scratchComponents = scratch.items.size();
			record.addedResourceClaims = claims.items.size();
			model.plans.push_back(std::move(record));

			planReceipts.emplace_back(planReceipt);
			staticProofReceipts.emplace_back(staticReceipt);
			setsByStock[plan.stockSha256].insert(plan.participantKey);
			plan.patchedSha256 = patchedSha256;
			plan.nativeProofReceipt = nativeReceipt;
			plans.push_back(std::move(plan));
		}

		// The plan matrix is ordered by target, then by canonical participant-set index.
		for (std::size_t index = 1; index < plans.size(); ++index) {
			const auto& previous = plans[index - 1];
			const auto& current = plans[index];
			const auto previousKey = std::pair(
				previous.target, ParticipantSetIndex(previous.participantKey, "artifact.patch_plans"));
			const auto currentKey = std::pair(
				current.target, ParticipantSetIndex(current.participantKey, "artifact.patch_plans"));
			RequireTrue(
				previousKey < currentKey,
				"artifact.patch_plans",
				"is not in canonical target and participant-set order");
		}
		for (auto& plan : plans)
			planLookup[plan.stockSha256 + "|" + plan.participantKey] = &plan;
		RequireNumber(
			static_cast<std::int64_t>(setsByStock.size()),
			static_cast<std::int64_t>(pins::kStockContracts.size()),
			"artifact.patch_plans stock coverage");
		for (const auto& stock : pins::kStockContracts) {
			const auto found = setsByStock.find(std::string(stock.stockSha256));
			RequireTrue(
				found != setsByStock.end(),
				"artifact.patch_plans",
				"does not cover a reviewed stock contract");
			const std::set<std::string> expected{ "SSGI", "SSGI+Wetness", "Wetness" };
			RequireTrue(
				found->second == expected,
				"artifact.patch_plans",
				"does not carry the atomic three-set plan group for a stock");
		}

		const auto& identityArray = ArrayAt(a_root, "stock_identities", "artifact");
		RequireNumber(
			static_cast<std::int64_t>(identityArray.items.size()),
			pins::kCompositeBlobs,
			"artifact.stock_identities");
		static constexpr std::array<std::string_view, 11> identityKeys{
			"declaration_commitment_sha256", "evidence_class", "occurrences", "outcomes",
			"resource_declarations", "stock_dcl_temps", "stock_length", "stock_sha1", "stock_sha256",
			"target", "target_status"
		};
		static constexpr std::array<std::string_view, 6> identityOccurrenceKeys{
			"byte_length", "dxbc_offset", "fxp_key", "fxp_ordinal", "marker_offset",
			"occurrence_receipt_sha256"
		};
		static constexpr std::array<std::string_view, 6> outcomeKeys{
			"evidence_class", "participant_set", "participants", "patch_plan", "reason", "verdict"
		};
		std::vector<Identity> identities;
		std::map<std::string, const Identity*> identityByHash;
		std::vector<std::string> aggregateRows;
		std::vector<std::string> associationRows;
		identities.reserve(identityArray.items.size());
		std::string previousHash;
		for (std::size_t index = 0; index < identityArray.items.size(); ++index) {
			const auto label = "artifact.stock_identities[" + std::to_string(index) + "]";
			const auto& row = identityArray.items[index];
			RequireKeys(row, identityKeys, label);
			Identity identity;
			identity.stockSha256 = DigestAt(row, "stock_sha256", label);
			RequireTrue(
				identity.stockSha256 > previousHash,
				label,
				"identities are not strictly ordered by SHA-256");
			previousHash = identity.stockSha256;
			identity.stockSha1 = DigestAt(row, "stock_sha1", label, 40);
			identity.stockLength = IntegerAt(row, "stock_length", label);
			RequireTrue(identity.stockLength > 0, Join(label, "stock_length"), "is not positive");
			const auto& temps = Child(row, "stock_dcl_temps", label);
			RequireTrue(
				temps.kind == ValueKind::kNull ||
					(temps.kind == ValueKind::kInteger && temps.integer > 0),
				Join(label, "stock_dcl_temps"),
				"is neither positive nor explicitly absent");
			identity.declarations = ValidateDeclarations(
				ArrayAt(row, "resource_declarations", label), Join(label, "resource_declarations"));
			identity.declarationCommitment = StringAt(row, "declaration_commitment_sha256", label);
			RequireCommitment(
				identity.declarationCommitment,
				Rcpt(CompactNode(ArrayAt(row, "resource_declarations", label))),
				Join(label, "declaration_commitment_sha256"));
			RequireText(StringAt(row, "target_status", label), kVerdictUnproven, Join(label, "target_status"));
			RequireText(StringAt(row, "evidence_class", label), kNativeStockBound, Join(label, "evidence_class"));
			identity.target = StringAt(row, "target", label);
			RequireText(identity.target, TargetFor(identity.stockSha256), Join(label, "target"));

			const auto& occurrenceRows = ArrayAt(row, "occurrences", label);
			RequireTrue(!occurrenceRows.items.empty(), Join(label, "occurrences"), "is empty");
			identity.occurrences = occurrenceRows.items.size();
			for (std::size_t occurrenceIndex = 0; occurrenceIndex < occurrenceRows.items.size(); ++occurrenceIndex) {
				const auto occurrenceLabel = label + ".occurrences[" + std::to_string(occurrenceIndex) + "]";
				const auto& occurrence = occurrenceRows.items[occurrenceIndex];
				RequireKeys(occurrence, identityOccurrenceKeys, occurrenceLabel);
				RequireNumber(
					IntegerAt(occurrence, "byte_length", occurrenceLabel),
					identity.stockLength,
					Join(occurrenceLabel, "byte_length"));
				RequireArchiveKey(
					StringAt(occurrence, "fxp_key", occurrenceLabel),
					Join(occurrenceLabel, "fxp_key"));
				DigestAt(occurrence, "occurrence_receipt_sha256", occurrenceLabel);
				associationRows.push_back(CompactObject({
					{ "byte_length", CompactNode(Child(occurrence, "byte_length", occurrenceLabel)) },
					{ "dxbc_offset", CompactNode(Child(occurrence, "dxbc_offset", occurrenceLabel)) },
					{ "fxp_key", CompactNode(Child(occurrence, "fxp_key", occurrenceLabel)) },
					{ "fxp_ordinal", CompactNode(Child(occurrence, "fxp_ordinal", occurrenceLabel)) },
					{ "marker_offset", CompactNode(Child(occurrence, "marker_offset", occurrenceLabel)) },
					{ "receipt", CompactNode(Child(occurrence, "occurrence_receipt_sha256", occurrenceLabel)) },
					{ "stock_sha1", CompactString(identity.stockSha1) },
					{ "stock_sha256", CompactString(identity.stockSha256) },
					{ "target", CompactString(identity.target) } }));
			}

			const auto& outcomes = ArrayAt(row, "outcomes", label);
			RequireNumber(static_cast<std::int64_t>(outcomes.items.size()), 4, Join(label, "outcomes"));
			for (std::size_t setIndex = 0; setIndex < outcomes.items.size(); ++setIndex) {
				const auto outcomeLabel = label + ".outcomes[" + std::to_string(setIndex) + "]";
				const auto& outcomeRow = outcomes.items[setIndex];
				RequireKeys(outcomeRow, outcomeKeys, outcomeLabel);
				Outcome outcome;
				outcome.participantSet = StringAt(outcomeRow, "participant_set", outcomeLabel);
				RequireText(
					outcome.participantSet,
					kParticipantSetKeys[setIndex],
					Join(outcomeLabel, "participant_set"));
				const auto outcomeParticipants = TextArray(
					ArrayAt(outcomeRow, "participants", outcomeLabel),
					Join(outcomeLabel, "participants"));
				RequireText(
					ParticipantKey(outcomeParticipants),
					outcome.participantSet,
					Join(outcomeLabel, "participants"));
				outcome.verdict = StringAt(outcomeRow, "verdict", outcomeLabel);
				outcome.reason = StringAt(outcomeRow, "reason", outcomeLabel);
				outcome.evidenceClass = StringAt(outcomeRow, "evidence_class", outcomeLabel);
				RequireTrue(!outcome.reason.empty(), Join(outcomeLabel, "reason"), "is empty");
				const auto& patchPlan = Child(outcomeRow, "patch_plan", outcomeLabel);
				RequireTrue(
					patchPlan.kind == ValueKind::kNull || patchPlan.kind == ValueKind::kString,
					Join(outcomeLabel, "patch_plan"),
					"has the wrong exact JSON type");
				outcome.hasPatchPlan = patchPlan.kind == ValueKind::kString;
				outcome.patchPlan = patchPlan.text;
				identity.outcomes.push_back(std::move(outcome));
			}
			aggregateRows.push_back(CompactObject({
				{ "stock_sha256", CompactString(identity.stockSha256) },
				{ "stock_dcl_temps", CompactNode(temps) },
				{ "declaration_commitment_sha256", CompactString(identity.declarationCommitment) } }));
			identities.push_back(std::move(identity));
		}
		for (const auto& identity : identities)
			identityByHash[identity.stockSha256] = &identity;

		// Outcomes and plan bindings are rederived, never trusted.
		for (const auto& identity : identities) {
			for (std::size_t setIndex = 0; setIndex < identity.outcomes.size(); ++setIndex) {
				const auto expected = DeriveOutcome(identity, setIndex, planLookup);
				const auto& actual = identity.outcomes[setIndex];
				RequireTrue(
					actual.verdict == expected.verdict && actual.reason == expected.reason &&
						actual.evidenceClass == expected.evidenceClass &&
						actual.hasPatchPlan == expected.hasPatchPlan &&
						actual.patchPlan == expected.patchPlan,
					"artifact.stock_identities outcome for " + identity.stockSha256,
					"is not rederivable from the plan matrix");
			}
			IdentityRecord record;
			record.stockSha256 = identity.stockSha256;
			record.stockSha1 = identity.stockSha1;
			record.target = identity.target;
			record.stockLength = identity.stockLength;
			record.occurrences = identity.occurrences;
			record.declarationCommitmentSha256 = identity.declarationCommitment;
			for (const auto& outcome : identity.outcomes)
				record.verdicts.push_back(outcome.verdict);
			model.identities.push_back(std::move(record));
		}
		for (const auto& plan : plans) {
			const auto found = identityByHash.find(plan.stockSha256);
			RequireTrue(
				found != identityByHash.end(),
				"artifact.patch_plans",
				"references an unknown stock identity");
			RequireNumber(
				plan.stockLength,
				found->second->stockLength,
				"artifact.patch_plans stock length");
			RequireText(plan.target, found->second->target, "artifact.patch_plans target");
			RequireTrue(
				plan.stockDeclarations.size() == found->second->declarations.size() &&
					std::equal(
						plan.stockDeclarations.begin(),
						plan.stockDeclarations.end(),
						found->second->declarations.begin(),
						[](const DeclarationRow& a_left, const DeclarationRow& a_right) {
							return a_left.reg == a_right.reg &&
							       a_left.dimension == a_right.dimension &&
							       a_left.returnToken == a_right.returnToken;
						}),
				"artifact.patch_plans stock declarations",
				"do not match the bound stock identity");
		}

		const auto& occurrenceArray = ArrayAt(a_root, "occurrences", "artifact");
		RequireNumber(
			static_cast<std::int64_t>(occurrenceArray.items.size()),
			pins::kCompositeOccurrences,
			"artifact.occurrences");
		static constexpr std::array<std::string_view, 14> occurrenceKeys{
			"byte_length", "dxbc_offset", "evidence_class", "fxp_key", "fxp_ordinal", "marker_offset",
			"occurrence_receipt_sha256", "participant_status", "profile", "stage", "stock_sha1",
			"stock_sha256", "target", "target_status"
		};
		std::set<std::string> seenReceipts;
		std::vector<std::string> occurrenceSetRows;
		std::vector<std::string> occurrenceAssociationRows;
		std::pair<std::int64_t, std::int64_t> previousCoordinate{ -1, -1 };
		std::map<std::string, std::map<std::string, std::size_t>> occurrenceTally;
		for (std::size_t index = 0; index < occurrenceArray.items.size(); ++index) {
			const auto label = "artifact.occurrences[" + std::to_string(index) + "]";
			const auto& row = occurrenceArray.items[index];
			RequireKeys(row, occurrenceKeys, label);
			OccurrenceRecord record;
			record.fxpOrdinal = IntegerAt(row, "fxp_ordinal", label);
			record.dxbcOffset = IntegerAt(row, "dxbc_offset", label);
			record.markerOffset = IntegerAt(row, "marker_offset", label);
			record.byteLength = IntegerAt(row, "byte_length", label);
			record.fxpKey = StringAt(row, "fxp_key", label);
			record.stockSha1 = DigestAt(row, "stock_sha1", label, 40);
			record.stockSha256 = DigestAt(row, "stock_sha256", label);
			record.target = StringAt(row, "target", label);
			record.receiptSha256 = DigestAt(row, "occurrence_receipt_sha256", label);
			RequireArchiveKey(record.fxpKey, Join(label, "fxp_key"));
			const std::pair<std::int64_t, std::int64_t> coordinate{ record.fxpOrdinal, record.dxbcOffset };
			RequireTrue(previousCoordinate < coordinate, label, "occurrences are not strictly ordered");
			previousCoordinate = coordinate;
			RequireTrue(
				record.fxpOrdinal >= pins::kFxpOrdinalFirst && record.fxpOrdinal <= pins::kFxpOrdinalLast,
				Join(label, "fxp_ordinal"),
				"is outside the Composite window");
			RequireTrue(
				record.markerOffset < record.dxbcOffset,
				Join(label, "marker_offset"),
				"does not precede its DXBC offset");
			RequireTrue(record.byteLength > 0, Join(label, "byte_length"), "is not positive");
			RequireTrue(
				seenReceipts.insert(record.receiptSha256).second,
				Join(label, "occurrence_receipt_sha256"),
				"is duplicated");
			RequireText(StringAt(row, "stage", label), "ps", Join(label, "stage"));
			RequireText(StringAt(row, "profile", label), "5_0", Join(label, "profile"));
			RequireText(StringAt(row, "target_status", label), kVerdictUnproven, Join(label, "target_status"));
			RequireText(StringAt(row, "evidence_class", label), kNativeStockBound, Join(label, "evidence_class"));

			const auto& statuses = ObjectAt(row, "participant_status", label);
			RequireKeys(statuses, kSortedParticipantSetKeys, Join(label, "participant_status"));
			const auto identity = identityByHash.find(record.stockSha256);
			RequireTrue(
				identity != identityByHash.end(),
				label,
				"references an unknown stock identity");
			for (const auto& outcome : identity->second->outcomes) {
				RequireText(
					StringAt(statuses, outcome.participantSet, Join(label, "participant_status")),
					outcome.verdict,
					Join(label, "participant_status"));
				++occurrenceTally[outcome.participantSet][outcome.verdict];
			}
			RequireText(record.target, identity->second->target, Join(label, "target"));
			RequireText(record.stockSha1, identity->second->stockSha1, Join(label, "stock_sha1"));
			RequireNumber(record.byteLength, identity->second->stockLength, Join(label, "byte_length"));

			occurrenceSetRows.push_back(CompactObject({
				{ "index", canonical::CompactInteger(static_cast<std::int64_t>(index)) },
				{ "fxp_ordinal", CompactNode(Child(row, "fxp_ordinal", label)) },
				{ "fxp_key", CompactNode(Child(row, "fxp_key", label)) },
				{ "dxbc_offset", CompactNode(Child(row, "dxbc_offset", label)) },
				{ "marker_offset", CompactNode(Child(row, "marker_offset", label)) },
				{ "byte_length", CompactNode(Child(row, "byte_length", label)) },
				{ "stage", CompactNode(Child(row, "stage", label)) },
				{ "profile", CompactNode(Child(row, "profile", label)) },
				{ "stock_sha1", CompactNode(Child(row, "stock_sha1", label)) },
				{ "stock_sha256", CompactNode(Child(row, "stock_sha256", label)) },
				{ "occurrence_receipt_sha256", CompactNode(Child(row, "occurrence_receipt_sha256", label)) } }));
			occurrenceAssociationRows.push_back(CompactObject({
				{ "byte_length", CompactNode(Child(row, "byte_length", label)) },
				{ "dxbc_offset", CompactNode(Child(row, "dxbc_offset", label)) },
				{ "fxp_key", CompactNode(Child(row, "fxp_key", label)) },
				{ "fxp_ordinal", CompactNode(Child(row, "fxp_ordinal", label)) },
				{ "marker_offset", CompactNode(Child(row, "marker_offset", label)) },
				{ "receipt", CompactNode(Child(row, "occurrence_receipt_sha256", label)) },
				{ "stock_sha1", CompactNode(Child(row, "stock_sha1", label)) },
				{ "stock_sha256", CompactNode(Child(row, "stock_sha256", label)) },
				{ "target", CompactNode(Child(row, "target", label)) } }));
			model.occurrences.push_back(std::move(record));
		}
		std::sort(associationRows.begin(), associationRows.end());
		std::sort(occurrenceAssociationRows.begin(), occurrenceAssociationRows.end());
		RequireTrue(
			associationRows == occurrenceAssociationRows,
			"artifact.occurrences",
			"do not project exactly onto the stock identity occurrence rows");

		ValidateByteProof(a_root, identities, occurrenceTally, plans, inventory);
		ValidateReceipts(a_root, occurrenceSetRows, aggregateRows, planReceipts, staticProofReceipts);

		model.release = StringAt(engineScope, "release", "artifact.engine_scope");
		model.engineContractScope = StringAt(engineScope, "engine_contract_scope", "artifact.engine_scope");
		model.executableSha256 = StringAt(engineScope, "executable_sha256", "artifact.engine_scope");
		model.lookupRva = StringAt(engineScope, "lookup_rva", "artifact.engine_scope");
		model.archiveMember = StringAt(archive, "member", "artifact.archive");
		model.archiveMemberSha256 = StringAt(archive, "member_sha256", "artifact.archive");
		model.subclass = StringAt(classification, "subclass", "artifact.classification");
		model.stage = StringAt(classification, "stage", "artifact.classification");
		model.profile = StringAt(classification, "profile", "artifact.classification");
		model.keyDomain = StringAt(classification, "key_domain", "artifact.classification");
		model.participantSetOrder = TextArray(
			ArrayAt(denominator, "participant_set_order", "artifact.denominator"),
			"artifact.denominator.participant_set_order");

		inventory.occurrences = model.occurrences.size();
		inventory.stockIdentities = model.identities.size();
		inventory.patchPlans = model.plans.size();
		inventory.passPlans = model.plans.size();
		inventory.occurrenceOutcomes = static_cast<std::size_t>(pins::kOccurrenceOutcomes);
		inventory.blobOutcomes = static_cast<std::size_t>(pins::kBlobOutcomes);
		inventory.normalizerRows = normalizer.items.size();
		inventory.runtimeAdmissible = BooleanAt(releasePolicy, "runtime_admissible", "artifact.release_policy");
		inventory.routeJoinRequired = BooleanAt(releasePolicy, "route_join_required", "artifact.release_policy");
		inventory.resolver = StringAt(routeAdmission, "resolver", "artifact.route_admission");
		inventory.suppression = StringAt(routeAdmission, "suppression", "artifact.route_admission");
		inventory.runtimeObservations = static_cast<std::size_t>(
			IntegerAt(routeAdmission, "runtime_observations", "artifact.route_admission"));
		inventory.joinReceipts = ArrayAt(routeAdmission, "join_receipts", "artifact.route_admission").items.size();
		inventory.routesAdmitted = static_cast<std::size_t>(
			IntegerAt(routeAdmission, "runtime_routes_admitted", "artifact.route_admission"));
		inventory.routesExclusive = static_cast<std::size_t>(
			IntegerAt(routeAdmission, "runtime_routes_exclusive", "artifact.route_admission"));
		inventory.ownershipPresent = false;
		inventory.keyDomain = model.keyDomain;
		inventory.contractsSha256 = StringAt(
			ObjectAt(a_root, "receipts", "artifact"), "contracts_sha256", "artifact.receipts");

		RequireTrue(!inventory.runtimeAdmissible, "artifact.release_policy", "claims runtime admissibility");
		RequireText(inventory.resolver, "no-match", "artifact.route_admission.resolver");
		RequireText(inventory.suppression, "none", "artifact.route_admission.suppression");
		RequireNumber(static_cast<std::int64_t>(inventory.runtimeObservations), 0, "artifact runtime observations");
		RequireNumber(static_cast<std::int64_t>(inventory.joinReceipts), 0, "artifact join receipts");
		RequireNumber(static_cast<std::int64_t>(inventory.routesAdmitted), 0, "artifact admitted routes");
		RequireNumber(static_cast<std::int64_t>(inventory.routesExclusive), 0, "artifact exclusive routes");
		RequireText(inventory.keyDomain, "archive_fxp_key", "artifact.classification.key_domain");
		return result;
	}
}
