#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Reviewed consumer facts. The producer contracts file is not copied, packaged, or read.
namespace fo4cs::offline::pins
{
	inline constexpr std::string_view kSchema = "fo4re.bsdf-composite-ambient-dxbc-patch-plans";
	inline constexpr std::int64_t kSchemaVersion = 2;
	inline constexpr std::uint64_t kArtifactLength = 871524;
	inline constexpr std::string_view kArtifactSha256 =
		"a721850c943d50568f7a1dbb91d08bae203c69aac253a3e81dd7dface32037e3";

	// Opaque: only the unshipped producer contracts file is its preimage.
	inline constexpr std::string_view kContractsSha256 =
		"3ea1eadd04e57141b93fe51f6d26d167f83a073c773d23cf3ebcebe6e792d54e";

	inline constexpr std::int64_t kCompositeOccurrences = 180;
	inline constexpr std::int64_t kCompositeBlobs = 78;
	inline constexpr std::int64_t kOccurrenceOutcomes = 720;
	inline constexpr std::int64_t kBlobOutcomes = 312;
	inline constexpr std::int64_t kPassPlans = 6;
	inline constexpr std::int64_t kPatchPlanKeyCount = 24;
	inline constexpr std::int64_t kStructuralCount = 3939;
	inline constexpr std::int64_t kByteScanCount = 3939;
	inline constexpr std::int64_t kUniqueShaderBlobs = 2026;
	inline constexpr std::int64_t kNativeCheckCount = 344;
	inline constexpr std::int64_t kNativeMutantCount = 12;
	inline constexpr std::int64_t kStaticMutationCount = 38;
	inline constexpr std::int64_t kArtifactMutationCount = 7;
	inline constexpr std::int64_t kStaticGateCount = 24;
	inline constexpr std::int64_t kNormalizerRowCount = 18;
	inline constexpr std::int64_t kFxpOrdinalFirst = 3401;
	inline constexpr std::int64_t kFxpOrdinalLast = 3580;

	inline constexpr std::string_view kEngineScope =
		R"json({"engine_contract_scope":"BSDFCompositeShader pixel lookup@fallout4-ae-1.11.221.0","executable_length":55293864,"executable_sha256":"428f9996cc4248e26c0f62f9fdd3eaf0e5eb305834b67ee5996538e593218b61","executable_version":"1.11.221.0","lookup_rva":"0x0226C7B0","release":"fallout4-ae-1.11.221.0"})json";
	inline constexpr std::string_view kArchive =
		R"json({"length":12845022,"logical_name":"Data/Fallout4 - Shaders.ba2","member":"shadersfx/shaders011.fxp","member_length":12844936,"member_sha256":"f3254023504c4bab250162284a28efe2ed79fbf7a9b81e26d0fa9f22660d1d1f","sha256":"4ac98b8fe72385f0cd13053bf5e81203fdaabe101377011e152457b705060659"})json";
	inline constexpr std::string_view kCensus =
		R"json({"build_receipt_sha256":"2e494dc8f4d030573455373dec8c4ba5970829b02d5bb656febea9383e6e5e81","byte_scan_count":3939,"occurrence_commitment_sha256":"a02081bb8c3f14f9fd23e9d13f10b6942699989105a24a48df74d90d5ea1592b","run_receipt_sha256":"8014d2eb9f037ed0c17c0523694a6e0eed2dacfb744a5927ffead2840005a093","structural_count":3939,"unique_shader_blobs":2026})json";
	inline constexpr std::string_view kClassificationScalars =
		R"json({"fxp_ordinal_first":3401,"fxp_ordinal_last":3580,"joins":[],"key_domain":"archive_fxp_key","key_domain_note":"fxp_key is not raw_technique, plugin_resolved_psid, or engine_lookup_psid.","occurrence_count":180,"profile":"5_0","runtime_observations":0,"scoped_occurrence_commitment_sha256":"44df794629115420315cef12efa29bfe2a1caae829fbb64d869848981e72d618","stage":"pixel","subclass":"BSDFCompositeShader","unique_blob_count":78})json";
	inline constexpr std::string_view kParticipants =
		R"json({"SSGI":{"coordinate_space":"integer_sv_position","instruction":"ld","mip":0,"neutral_value":"all signed forms of zero","resource_register":0,"resource_type":"Texture2D<float4>","sampler":null},"Wetness":{"coordinate_space":"integer_sv_position","instruction":"ld","mip":0,"neutral_value":"zero","resource_register":13,"resource_type":"Texture2D<float>","sampler":null}})json";
	inline constexpr std::string_view kDenominator =
		R"json({"blob_outcomes":312,"composite_blobs":78,"composite_occurrences":180,"occurrence_outcomes":720,"participant_set_order":["stock","SSGI","Wetness","SSGI+Wetness"],"pass_patch_plans":6})json";
	inline constexpr std::string_view kFallbackGraph =
		R"json({"SSGI":[],"SSGI+Wetness":[],"Wetness":[],"note":"no fallback edge is proven; each participant set is an independent offline product","stock":[]})json";
	inline constexpr std::string_view kReleasePolicy =
		R"json({"build_mismatch_action":"stock","hash_mismatch_action":"stock","release_scope":"archive-mechanics-and-byte-proof-only","route_join_required":true,"runtime_admissible":false,"status":"provisional","unknown_action":"stock"})json";
	inline constexpr std::string_view kRouteAdmission =
		R"json({"join_receipts":[],"reason":"no independent runtime-to-archive route join exists","resolver":"no-match","runtime_observations":0,"runtime_routes_admitted":0,"runtime_routes_exclusive":0,"suppression":"none"})json";
	inline constexpr std::string_view kEvidencePolicy =
		R"json({"artifact_attacks":"unbound","binding_controls":"format-realistic-synthetic","classes":["native-stock-bound","source-recompiled-identity","format-realistic-synthetic","unbound"],"mechanical_pass_requires":"native-stock-bound","source_recompiled_identity_grants_fidelity":false})json";
	inline constexpr std::string_view kCollisionCounts =
		R"json({"SSGI+Wetness_fail_blobs":30,"SSGI+Wetness_fail_occurrences":78,"SSGI_fail_blobs":30,"SSGI_fail_occurrences":78,"t0_blobs":30,"t0_occurrences":78,"t13_blobs":0,"t13_occurrences":0})json";
	inline constexpr std::string_view kParticipantTallies =
		R"json({"SSGI":{"by_blob":{"FAIL":30,"PASS":2,"UNPROVEN":46},"by_occurrence":{"FAIL":78,"PASS":2,"UNPROVEN":100}},"SSGI+Wetness":{"by_blob":{"FAIL":30,"PASS":2,"UNPROVEN":46},"by_occurrence":{"FAIL":78,"PASS":2,"UNPROVEN":100}},"Wetness":{"by_blob":{"FAIL":0,"PASS":2,"UNPROVEN":76},"by_occurrence":{"FAIL":0,"PASS":2,"UNPROVEN":178}},"stock":{"by_blob":{"FAIL":0,"PASS":78,"UNPROVEN":0},"by_occurrence":{"FAIL":0,"PASS":180,"UNPROVEN":0}}})json";
	inline constexpr std::string_view kArtifactValidationMutations =
		R"json([{"evidence_class":"unbound","expected_gate":"artifact-validation","mutation":"malformed_artifact"},{"evidence_class":"unbound","expected_gate":"artifact-validation","mutation":"stale_artifact"},{"evidence_class":"unbound","expected_gate":"artifact-validation","mutation":"partial_artifact"},{"evidence_class":"unbound","expected_gate":"artifact-validation","mutation":"wrong_runtime_artifact"},{"evidence_class":"unbound","expected_gate":"artifact-validation","mutation":"wrong_denominator_artifact"},{"evidence_class":"unbound","expected_gate":"artifact-validation","mutation":"wrong_stock_artifact"},{"evidence_class":"unbound","expected_gate":"artifact-validation","mutation":"wrong_participant_set_artifact"}])json";
	inline constexpr std::string_view kHarnessBuild =
		R"json({"binary_length":348160,"binary_name":"composite_patch_exec_diff.exe","binary_sha256":"5197a783285f15aaa9d6bf8917e433df0ad6af0dd814cf177fddd8d1491a282b","build_script_sha256":"3ae6fec883115330c8f78c52de9c2e5a0eb424436f18dae466d3b153e52b4b9a","canonical_source_path":"Z:\\fo4re-composite-build\\composite_patch_exec_diff-main.cpp","canonical_temporary_directory":"Z:\\fo4re-composite-build\\tmp","canonical_working_directory":"Z:\\fo4re-composite-build","compile_flags":"/nologo /std:c++17 /O2 /EHsc /W4 /WX /Brepro /experimental:deterministic /pathmap:<build-root>=Z:\\fo4re-composite-build","compiler_length":893288,"compiler_sha256":"c94cdac6a780142920110e5cb8b7339817029eead696e0e97700b45e03216a00","compiler_version":"19.51.36252.0","environment_variables_cleared":["CL","_CL_","LINK","_LINK_"],"link_flags":"/Brepro /INCREMENTAL:NO /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib bcrypt.lib","linker_length":3700576,"linker_sha256":"f233b8e337cec96a69868a8cde676808bfa81152493968d0b27b7cd0daac15be","linker_version":"14.51.36252.0","object_name":"composite_patch_exec_diff.obj","receipt_sha256":"a636cc82dab220e8f66f6455739a1880a1e0db2a414ba5ea597560aa3e5f457a","schema":"fo4re.composite-exec-build","schema_version":2,"selected_build_script_object":"28a903469173b1b0cc04342fdaf31219e7952907","selected_source_object":"9ffa75ad98d74358860d81b340cb957c2563a24f","source_sha256":"dacc0d7efaac95a92159dbdb346c641854f3314b4d26bc53c602bc54329ca4c1"})json";
	inline constexpr std::string_view kEvidenceClassCounts =
		R"json({"format-realistic-synthetic":20,"native-stock-bound":336,"source-recompiled-identity":0,"unbound":0})json";
	inline constexpr std::string_view kSemanticCommitments =
		R"json({"checks_sha256":"02d8247840f86a6c7607d3067f2e22cb4e9df7b3cc230e1a2c21a395e8d5bd65","evidence_classes_sha256":"d6418bbe9ca4aedc3fafb0dff80a48bd59dd0d6f8591e461fdcb4d5d3b2456e8","mutants_sha256":"af54524b3418be5f2a818bed195835273ff0324fbe89f4f8407df5cdcf85b807","plan_receipts_sha256":"ec852dc91e8af38797025aa6a2b5f63c5e2636d792a235c34d7821aa794ae65a"})json";
	inline constexpr std::string_view kNativePlanReceipts =
		R"json({"composite-ambient-bb66b923:SSGI":"dac0c0088c4a04870028823a94c7b454d26f6684db4a1dfd7050fba77e87bac8","composite-ambient-bb66b923:SSGI+Wetness":"755e6b390080ea28ac9d6f76351674845afcaadac5eb8308f9fc82fa68442b61","composite-ambient-bb66b923:Wetness":"59f4207f10be5ae602db08cc13f9b210e64b222a07bcfcfbbda143950b35d43b","composite-ambient-c36d04cc:SSGI":"3126cd9ad26f1d0e1030ffb1a8d04ff22691cc79e6bbb6187a17f8ec58f6636a","composite-ambient-c36d04cc:SSGI+Wetness":"b43fe748e9ec06a1eaa3f5f872516058c2b81931cb271a7f933d8575be0e6494","composite-ambient-c36d04cc:Wetness":"a61d98cdcb4e5ca7e1004bb3eece8f1581de33e75a8280c3842ff5b4af8ebd10"})json";
	inline constexpr std::string_view kReceiptPlanReceipts =
		R"json(["1c07841487c862c175f351b07a5f37782c82236f807f110e996c37b4d2e89b1a","720f3d59411357266e15fb87cfa709d6b2d4cd246e5ff7dbf5a087a894b7cae7","8a2bd61f2199f27930acf68e5b28da12120faf092cd852145a933b979851a527","8f43933b4d9c9fcad9796a09538ab2202fbf1abec2dfd375fd9a299ddc3a5afa","30739897f6c24906af946194ecfff1d86fbef98d428954436dc567e3dea2335d","2263717b358f575c1d36ceeb7153857f3b38ee3540cd6b12226ab78e1f73aea7"])json";
	inline constexpr std::string_view kReceiptStaticProofReceipts =
		R"json(["3a064ac07d988dc428d1d389943c27db35515dbe9d348750b42fa49106942783","c0a08777686c8a05b47fe9686eaae5cbe67bc48b9870b06f26f4aa37d4e434b3","db0a5162f1972a7a18548cf0c1d4eff12a2c96b5aca48b33903b3347bdcfdee6","c157d3562ec376bc676c7d2ca20ef9a450ba508bd17c36bb1fc668814f55a1b3","05232b0a887240f99221c16bc0bb86fe52bf81b4f30b59e9c233c816c8395c44","548bc03f83b52b6f8ee0f6d25eb7e464f39d902b7baa3d256a43d0213cc18358"])json";

	inline constexpr std::array<std::string_view, 24> kStaticGates{
		"stock_identity_exact",
		"stock_and_patched_ps_5_0",
		"input_output_signatures_identical",
		"complete_stock_declaration_array_exact",
		"injected_slots_absent_in_stock",
		"patched_declarations_add_only_requested_slots",
		"no_sampler_added",
		"dcl_temps_preimage_and_result_exact",
		"all_local_preimages_exact",
		"ordered_shex_edits_only",
		"isgn_osgn_and_unrelated_chunks_identical",
		"single_serialization_and_checksum_rewrite",
		"reverse_edits_recover_stock",
		"patched_identity_exact",
		"output_is_only_o0_xyzw",
		"participant_order_exact",
		"stock_avoids_fresh_scratch_range",
		"typed_opcode_operand_liveness",
		"unknown_or_raw_opcode_analysis_unavailable",
		"all_inserted_register_writes_classified",
		"scratch_path_defined",
		"participant_stock_writes_guard_dominated",
		"signed_zero_guard_dominance_static",
		"all_temps_below_dcl_temps"
	};

	struct NormalizerPin
	{
		std::string_view formulaOutput;
		std::string_view fxpKey;
		std::int64_t fxpOrdinal;
		std::string_view stockSha256;
	};

	inline constexpr std::array<NormalizerPin, 18> kNormalizerDiagnostic{{
		{ "0x00000020", "0x00000420", 3404, "760810e9105cc53e2e8a26136d47f814ac8dbf5c55cfead7ebf199b82fdd46f0" },
		{ "0x00020021", "0x00020025", 3415, "ddfa93f379ded0c7e5939fcba927cde828a3be831659143f853f7d5675bf89d4" },
		{ "0x00020801", "0x00020805", 3426, "e88ddad37b353e446d04b8026c73dcdd848afa2d32e5b74c847b83cb11bebcfd" },
		{ "0x00020821", "0x00020825", 3429, "eabe4a78f464d6f6ee96ecbd1c7ca1017cceea2f725ca3fd12c1e07b142d0c34" },
		{ "0x00028021", "0x00028025", 3437, "ddfa93f379ded0c7e5939fcba927cde828a3be831659143f853f7d5675bf89d4" },
		{ "0x00030021", "0x00030025", 3451, "1bdd0a381c5c143431766603e35758cc90d5c252a15062643fc35b7089639caf" },
		{ "0x00000801", "0x00000805", 3465, "e88ddad37b353e446d04b8026c73dcdd848afa2d32e5b74c847b83cb11bebcfd" },
		{ "0x00000821", "0x00000825", 3468, "eabe4a78f464d6f6ee96ecbd1c7ca1017cceea2f725ca3fd12c1e07b142d0c34" },
		{ "0x00040801", "0x00040805", 3486, "e88ddad37b353e446d04b8026c73dcdd848afa2d32e5b74c847b83cb11bebcfd" },
		{ "0x00040821", "0x00040825", 3489, "eabe4a78f464d6f6ee96ecbd1c7ca1017cceea2f725ca3fd12c1e07b142d0c34" },
		{ "0x00008021", "0x00008025", 3498, "ddfa93f379ded0c7e5939fcba927cde828a3be831659143f853f7d5675bf89d4" },
		{ "0x00000021", "0x00000025", 3506, "ddfa93f379ded0c7e5939fcba927cde828a3be831659143f853f7d5675bf89d4" },
		{ "0x00060801", "0x00060805", 3509, "e88ddad37b353e446d04b8026c73dcdd848afa2d32e5b74c847b83cb11bebcfd" },
		{ "0x00060821", "0x00060825", 3512, "eabe4a78f464d6f6ee96ecbd1c7ca1017cceea2f725ca3fd12c1e07b142d0c34" },
		{ "0x00070801", "0x00070805", 3520, "e88ddad37b353e446d04b8026c73dcdd848afa2d32e5b74c847b83cb11bebcfd" },
		{ "0x00070821", "0x00070825", 3523, "da4378e7839cb2aa41028b732383ed4d785810d3beb3637d65a61bb0fad1fdf3" },
		{ "0x00010021", "0x00010025", 3537, "1bdd0a381c5c143431766603e35758cc90d5c252a15062643fc35b7089639caf" },
		{ "0x00018021", "0x00018025", 3578, "1bdd0a381c5c143431766603e35758cc90d5c252a15062643fc35b7089639caf" }
	}};

	struct StockContractPin
	{
		std::string_view target;
		std::string_view recipeId;
		std::string_view stockSha1;
		std::string_view stockSha256;
		std::int64_t stockLength;
		std::int64_t stockDclTemps;
	};

	inline constexpr std::array<StockContractPin, 2> kStockContracts{{
		{ "composite-ambient-bb66b923", "bsdf-composite-ambient-bb66b923-v2", "6d726d0fe6b6c474da30edbffcecfa067c795873", "bb66b923c180b08452607ccea7a6f1122b5787ba85fbd0cb7e4e7c2c1f62949c", 10756, 11 },
		{ "composite-ambient-c36d04cc", "bsdf-composite-ambient-c36d04cc-v2", "2b6e36c08aca7ff0a3bd10da326e00b3b0367383", "c36d04cc8e593d4840607ff3abe1f48cd2ee2db302a16101070f657a608094cd", 10972, 11 }
	}};

	// Single-cause proof mutations; family coverage multiplies these by bb and c36.
	struct MutationPin
	{
		std::string_view name;
		std::string_view bucket;
		std::string_view participants;
		std::string_view causeScope;
		std::string_view expectedGate;
		std::string_view semanticDelta;
		std::string_view evidenceClass;
		bool native;
	};

	inline constexpr std::array<MutationPin, 32> kRequiredMutations{{
		{ "omit_t0_load", "ssgi-load", R"json(["SSGI"])json", "executable", "feature-resource", "required-t0-load-replaced-by-zero", "native-stock-bound", false },
		{ "wrong_t0_slot", "ssgi-slot", R"json(["SSGI"])json", "executable", "feature-resource", "required-t0-load-reads-t4", "native-stock-bound", false },
		{ "omit_t13_load", "wetness-load", R"json(["Wetness"])json", "executable", "feature-resource", "required-t13-load-replaced-by-zero", "native-stock-bound", false },
		{ "wrong_t13_slot", "wetness-slot", R"json(["Wetness"])json", "executable", "feature-resource", "required-t13-load-reads-t14", "native-stock-bound", false },
		{ "declaration_slot_drift", "declaration-slot", R"json(["SSGI"])json", "executable", "declaration-shape", "ssgi-declaration-register-0-to-1", "native-stock-bound", false },
		{ "declaration_dimension_drift", "declaration-dimension", R"json(["SSGI"])json", "executable", "declaration-delta", "ssgi-declaration-dimension-bit-11-flipped", "native-stock-bound", false },
		{ "declaration_return_type_drift", "declaration-return-type", R"json(["SSGI"])json", "executable", "declaration-delta", "ssgi-return-token-5555-to-4444", "native-stock-bound", false },
		{ "declaration_order_drift", "declaration-order", R"json(["SSGI"])json", "executable", "declaration-shape", "ssgi-declaration-moved-to-t13-anchor", "native-stock-bound", false },
		{ "declaration_multiplicity_drift", "declaration-multiplicity", R"json(["SSGI"])json", "executable", "declaration-shape", "ssgi-declaration-duplicated", "native-stock-bound", false },
		{ "omit_wetness_attenuation", "wetness-attenuation", R"json(["Wetness"])json", "executable", "native-active-output", "wetness-attenuation-replaced-by-copy", "native-stock-bound", true },
		{ "invert_wetness_attenuation", "wetness-attenuation", R"json(["Wetness"])json", "executable", "native-active-output", "wetness-attenuation-factor-inverted", "native-stock-bound", true },
		{ "omit_wetness_film", "wetness-film", R"json(["Wetness"])json", "executable", "native-active-output", "wetness-film-mad-replaced-by-copy", "native-stock-bound", true },
		{ "move_wetness_after_t9", "wetness-order", R"json(["Wetness"])json", "executable", "edit-manifest", "wetness-body-moved-to-ssgi-anchor", "native-stock-bound", false },
		{ "move_wetness_after_fog", "wetness-order", R"json(["Wetness"])json", "executable", "edit-manifest", "wetness-body-moved-to-output-alpha-anchor", "native-stock-bound", false },
		{ "reinterpret_t12", "t12-confinement", R"json(["Wetness"])json", "executable", "t12-confinement", "wetness-resource-register-13-to-12", "native-stock-bound", false },
		{ "omit_ssgi_add", "ssgi-add", R"json(["SSGI"])json", "executable", "required-stock-write", "ssgi-rgb-add-removed", "native-stock-bound", false },
		{ "move_ssgi_before_t9", "ssgi-order", R"json(["SSGI"])json", "executable", "native-active-output", "ssgi-body-moved-to-wetness-anchor", "native-stock-bound", true },
		{ "move_ssgi_after_fog", "ssgi-order", R"json(["SSGI"])json", "executable", "edit-manifest", "ssgi-body-moved-to-output-alpha-anchor", "native-stock-bound", false },
		{ "omit_ssgi_zero_branch", "ssgi-guard-dominance", R"json(["SSGI"])json", "executable", "guard-dominance", "ssgi-zero-guard-removed", "native-stock-bound", false },
		{ "swap_combined_order", "combined-order", R"json(["SSGI","Wetness"])json", "executable", "native-active-output", "combined-wetness-and-ssgi-anchors-swapped", "native-stock-bound", true },
		{ "reuse_live_temp", "scratch-confinement", R"json(["SSGI","Wetness"])json", "executable", "scratch-confinement", "ssgi-scratch-base-19-to-live-r9", "native-stock-bound", false },
		{ "omit_dcl_temps", "temporary-declaration", R"json(["SSGI"])json", "executable", "edit-manifest", "temporary-count-replacement-removed", "native-stock-bound", false },
		{ "corrupt_dcl_temps", "temporary-declaration", R"json(["SSGI"])json", "executable", "temporary-bound", "temporary-count-replaced-with-12", "native-stock-bound", false },
		{ "change_alpha", "alpha-preservation", R"json(["SSGI"])json", "executable", "native-alpha-output", "guarded-o0-w-write-inserted", "native-stock-bound", true },
		{ "stale_checksum", "checksum", R"json(["SSGI"])json", "container", "checksum", "container-checksum-bit-flipped", "native-stock-bound", false },
		{ "malformed_artifact", "artifact-shape", R"json([])json", "artifact", "artifact-validation", "artifact-shape-malformed", "unbound", false },
		{ "stale_artifact", "artifact-receipt", R"json([])json", "artifact", "artifact-validation", "artifact-receipt-stale", "unbound", false },
		{ "partial_artifact", "artifact-denominator", R"json([])json", "artifact", "artifact-validation", "artifact-denominator-row-dropped", "unbound", false },
		{ "wrong_runtime_artifact", "artifact-runtime", R"json([])json", "artifact", "artifact-validation", "artifact-runtime-substituted", "unbound", false },
		{ "wrong_denominator_artifact", "artifact-denominator", R"json([])json", "artifact", "artifact-validation", "artifact-denominator-substituted", "unbound", false },
		{ "wrong_stock_artifact", "artifact-stock", R"json([])json", "artifact", "artifact-validation", "artifact-stock-identity-substituted", "unbound", false },
		{ "wrong_participant_set_artifact", "artifact-participant-set", R"json([])json", "artifact", "artifact-validation", "artifact-participant-set-substituted", "unbound", false }
	}};

	// The nineteen-key reviewed plan pin, keyed by plan id.
	struct PlanPin
	{
		std::string_view planId;
		std::string_view recipeId;
		std::string_view target;
		std::string_view stockSha256;
		std::string_view participants;
		std::string_view proofStatus;
		std::string_view mechanicalEvidenceClass;
		std::string_view stockDeclarationCommitment;
		std::string_view patchedDeclarationCommitment;
		std::string_view patched;
		std::string_view editCommitment;
		std::string_view staticProofCommitment;
		std::string_view addedResourceClaimsCommitment;
		std::string_view scratchComponentsCommitment;
		std::string_view planReceipt;
		std::string_view staticProofReceipt;
		std::string_view nativeProofReceipt;
		std::int64_t stockLength;
		std::int64_t stockDclTemps;
		std::int64_t patchedDclTemps;
	};

	inline constexpr std::array<PlanPin, 6> kExpectedPlans{{
		{
			"composite-ambient-bb66b923:SSGI",
			"bsdf-composite-ambient-bb66b923-v2",
			"composite-ambient-bb66b923",
			"bb66b923c180b08452607ccea7a6f1122b5787ba85fbd0cb7e4e7c2c1f62949c",
			R"json(["SSGI"])json",
			"PASS",
			"native-stock-bound",
			"dc3641de11cd59a31e37fa243bcbb965d13a5f617f7287d79f079bae16b4a47a",
			"e27dd1de9cd13439aea95c1ba128af224d53c06d67f9c936f048ebb125d852b2",
			R"json({"checksum":"bc4ae6b5e09d9aa77a2d383c1700352c","length":10980,"sha1":"2ee351814a113ca7061bbd94d0c0ac3e97f49f76","sha256":"6ce1b92e6ab9e6318c55093887fe9918522e0437300aaac892f7370416d6d83e"})json",
			"bdc7d9c90384c1578216ab000fca6196f3cd5cb33f636388ba359a6bdd8cdb16",
			"f72652319e102f892f3eba9804ac7ccada838bae68f1c835a69e06d697fcfa6a",
			"a8ae57cbfc6b27d086004ab5fc1388dc6b0434dd39257f7ad35c953df54517d8",
			"9a03f8211528e317c670286b180e9eaddef6eb932f94ca786f028a3b240bbe35",
			"1c07841487c862c175f351b07a5f37782c82236f807f110e996c37b4d2e89b1a",
			"3a064ac07d988dc428d1d389943c27db35515dbe9d348750b42fa49106942783",
			"dac0c0088c4a04870028823a94c7b454d26f6684db4a1dfd7050fba77e87bac8",
			10756,
			11,
			13
		},
		{
			"composite-ambient-bb66b923:Wetness",
			"bsdf-composite-ambient-bb66b923-v2",
			"composite-ambient-bb66b923",
			"bb66b923c180b08452607ccea7a6f1122b5787ba85fbd0cb7e4e7c2c1f62949c",
			R"json(["Wetness"])json",
			"PASS",
			"native-stock-bound",
			"dc3641de11cd59a31e37fa243bcbb965d13a5f617f7287d79f079bae16b4a47a",
			"de23228bfb297d6c5eef5ea0c8aed2519918eaf73c3f3325add140e3412d53ed",
			R"json({"checksum":"5bdb8bd6db54dc93d3b1c469b2c55946","length":12460,"sha1":"13d9331e0abf4395d2fd2746e9c15f909fe3c01a","sha256":"282eceb3b5230c6845912617b67d31509da7dd56a96d48ce292b417f3fba7cce"})json",
			"4c762de21709ee5ddf32c2ca4ddc6051421c3fe4d2759763a83668cb48cd7e4d",
			"6099ae26467cfdd430d271637c36d53d16e2c939529ba0d8b6fea351dee5107e",
			"f13b3d4411bd4172804b4ee5ea46389a9ef5711eb873d533720f1f2ae0c26b01",
			"5ada9eb526ce2dbf800bba611ccea14fc5bc7a4b107e3be444ad2d59071743fa",
			"720f3d59411357266e15fb87cfa709d6b2d4cd246e5ff7dbf5a087a894b7cae7",
			"c0a08777686c8a05b47fe9686eaae5cbe67bc48b9870b06f26f4aa37d4e434b3",
			"59f4207f10be5ae602db08cc13f9b210e64b222a07bcfcfbbda143950b35d43b",
			10756,
			11,
			19
		},
		{
			"composite-ambient-bb66b923:SSGI+Wetness",
			"bsdf-composite-ambient-bb66b923-v2",
			"composite-ambient-bb66b923",
			"bb66b923c180b08452607ccea7a6f1122b5787ba85fbd0cb7e4e7c2c1f62949c",
			R"json(["SSGI","Wetness"])json",
			"PASS",
			"native-stock-bound",
			"dc3641de11cd59a31e37fa243bcbb965d13a5f617f7287d79f079bae16b4a47a",
			"7dc7470be52ca7cfbe07659644b3ac56420f4d5886efb9cce524ca2daf8abd78",
			R"json({"checksum":"457f4951fea103463c383e0a967ae6f3","length":12684,"sha1":"77ba0bc1445d5f1bd4aadd1b0e990faea7ca8fe2","sha256":"ca474ad501d8499652c2693e5c8dac2c61a2445d0e213da76ca46b83ec0f4d9a"})json",
			"6ff99d38198587a1e5a111be60975f3386721114f719fd0ed2a0ae3c6a4346bc",
			"9bf7bf8c9f55dfffea82e7eb70168664347710ac8e5bfbb9d396ff044131e069",
			"5f6e229ea35ae184b138fad87e64da7c4fe271b45f2465575dae1e0a8719cdf7",
			"6efdfb639227398b9169113f9af796337f8d19344fa3e91159a71d5031683657",
			"8a2bd61f2199f27930acf68e5b28da12120faf092cd852145a933b979851a527",
			"db0a5162f1972a7a18548cf0c1d4eff12a2c96b5aca48b33903b3347bdcfdee6",
			"755e6b390080ea28ac9d6f76351674845afcaadac5eb8308f9fc82fa68442b61",
			10756,
			11,
			21
		},
		{
			"composite-ambient-c36d04cc:SSGI",
			"bsdf-composite-ambient-c36d04cc-v2",
			"composite-ambient-c36d04cc",
			"c36d04cc8e593d4840607ff3abe1f48cd2ee2db302a16101070f657a608094cd",
			R"json(["SSGI"])json",
			"PASS",
			"native-stock-bound",
			"27c9582b508ee19425b65eb8e086286a62569145743ca63eda9a162096c72429",
			"243e257aba00f2a50e0f069b481b976ed13ab1c7589250030cd0db16219092a0",
			R"json({"checksum":"702ea0e406fb7ebb53878f08a4c6f087","length":11196,"sha1":"11a48cc2bad91184546c82546f5972d0a3e984a0","sha256":"9b9de14fc4975440255d9287c9317a1a171261c03c0f7653a02c2f816ce0d8b3"})json",
			"a6b35e710b9676d43e3da05577c5e3831260f8ab66805019706081367490b3b0",
			"054160bd49c8d7c9eb8779331e28931982df4c89856f19386144f1cc5fc2ff60",
			"a8ae57cbfc6b27d086004ab5fc1388dc6b0434dd39257f7ad35c953df54517d8",
			"c1e1a7b36d7401213e48ff40d5464fe09abf28d277241bc01841fde2ee20b7c5",
			"8f43933b4d9c9fcad9796a09538ab2202fbf1abec2dfd375fd9a299ddc3a5afa",
			"c157d3562ec376bc676c7d2ca20ef9a450ba508bd17c36bb1fc668814f55a1b3",
			"3126cd9ad26f1d0e1030ffb1a8d04ff22691cc79e6bbb6187a17f8ec58f6636a",
			10972,
			11,
			13
		},
		{
			"composite-ambient-c36d04cc:Wetness",
			"bsdf-composite-ambient-c36d04cc-v2",
			"composite-ambient-c36d04cc",
			"c36d04cc8e593d4840607ff3abe1f48cd2ee2db302a16101070f657a608094cd",
			R"json(["Wetness"])json",
			"PASS",
			"native-stock-bound",
			"27c9582b508ee19425b65eb8e086286a62569145743ca63eda9a162096c72429",
			"7b289114c63c3d48f0c58d7cbade457978b5387229fb2f8829b2c3c8f61529f5",
			R"json({"checksum":"915eb7eaca9462356f04db71127eec3a","length":12676,"sha1":"df0f5b10f09c3fb95bf52bbca299945b38b98f89","sha256":"eb3f088ef9f7c8c979c4d80329211e479cc7105763d141d5d4f366e7408b3458"})json",
			"440bd26cd3220eab6f30a1fc4555d656864349aad3ad46a984298f6bad74a095",
			"e8426ae68f87c4b399e3822f75681b2efa890030dfa0b55caf3814041118d72b",
			"f13b3d4411bd4172804b4ee5ea46389a9ef5711eb873d533720f1f2ae0c26b01",
			"30b73c3f4bbca5aef0bbe1764674cc04be5d62a5eb08bb86052c802225d2db33",
			"30739897f6c24906af946194ecfff1d86fbef98d428954436dc567e3dea2335d",
			"05232b0a887240f99221c16bc0bb86fe52bf81b4f30b59e9c233c816c8395c44",
			"a61d98cdcb4e5ca7e1004bb3eece8f1581de33e75a8280c3842ff5b4af8ebd10",
			10972,
			11,
			19
		},
		{
			"composite-ambient-c36d04cc:SSGI+Wetness",
			"bsdf-composite-ambient-c36d04cc-v2",
			"composite-ambient-c36d04cc",
			"c36d04cc8e593d4840607ff3abe1f48cd2ee2db302a16101070f657a608094cd",
			R"json(["SSGI","Wetness"])json",
			"PASS",
			"native-stock-bound",
			"27c9582b508ee19425b65eb8e086286a62569145743ca63eda9a162096c72429",
			"aa0c7c76f9031a98c83d26ba16fe7485fac8a366f0bd0c8e10dbd37252414126",
			R"json({"checksum":"2bd1d64b1166b907a9ced35b971604e2","length":12900,"sha1":"3e244972f4947812e6cf1ab0c8c8cea8c4dd5345","sha256":"70232d873aafbf9b1b559606b040e53fb0e616080d911506e518d96eafeee7da"})json",
			"e561c0b7fe745bd2e1fc653cff713971883f2580cd3e83c3f59f13bed5796adf",
			"fb27a905522c5f60414df6068765c5c8d8e250dd873176ba0c53d2ba17a9ea35",
			"5f6e229ea35ae184b138fad87e64da7c4fe271b45f2465575dae1e0a8719cdf7",
			"113477a332d82adec7cca968537e6f4661760d5c3c1ce94b76aa41ee1db91180",
			"2263717b358f575c1d36ceeb7153857f3b38ee3540cd6b12226ab78e1f73aea7",
			"548bc03f83b52b6f8ee0f6d25eb7e464f39d902b7baa3d256a43d0213cc18358",
			"b43fe748e9ec06a1eaa3f5f872516058c2b81931cb271a7f933d8575be0e6494",
			10972,
			11,
			21
		}
	}};

	// The fifteen-key reviewed native proof pin and the thirteen-key reviewed census pin.
	inline constexpr std::string_view kExpectedNativeProof =
		R"json({"adapter":"raw-dxbc-d3d11-warp-composite-v1","check_count":344,"checks_commitment_sha256":"6bae66b706c3fc5860826e7e70debdffb0f63e07e5b7884843383251025a6e3b","evidence_class_counts":{"format-realistic-synthetic":20,"native-stock-bound":336,"source-recompiled-identity":0,"unbound":0},"execution_plan_sha256":"84ccd22f2adc15f4d84a08c74854ff0baf46818b51531c081de5579f977f152c","harness_build":{"binary_length":348160,"binary_name":"composite_patch_exec_diff.exe","binary_sha256":"5197a783285f15aaa9d6bf8917e433df0ad6af0dd814cf177fddd8d1491a282b","build_script_sha256":"3ae6fec883115330c8f78c52de9c2e5a0eb424436f18dae466d3b153e52b4b9a","canonical_source_path":"Z:\\fo4re-composite-build\\composite_patch_exec_diff-main.cpp","canonical_temporary_directory":"Z:\\fo4re-composite-build\\tmp","canonical_working_directory":"Z:\\fo4re-composite-build","compile_flags":"/nologo /std:c++17 /O2 /EHsc /W4 /WX /Brepro /experimental:deterministic /pathmap:<build-root>=Z:\\fo4re-composite-build","compiler_length":893288,"compiler_sha256":"c94cdac6a780142920110e5cb8b7339817029eead696e0e97700b45e03216a00","compiler_version":"19.51.36252.0","environment_variables_cleared":["CL","_CL_","LINK","_LINK_"],"link_flags":"/Brepro /INCREMENTAL:NO /OPT:REF /OPT:ICF d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib bcrypt.lib","linker_length":3700576,"linker_sha256":"f233b8e337cec96a69868a8cde676808bfa81152493968d0b27b7cd0daac15be","linker_version":"14.51.36252.0","object_name":"composite_patch_exec_diff.obj","receipt_sha256":"a636cc82dab220e8f66f6455739a1880a1e0db2a414ba5ea597560aa3e5f457a","schema":"fo4re.composite-exec-build","schema_version":2,"selected_build_script_object":"28a903469173b1b0cc04342fdaf31219e7952907","selected_source_object":"9ffa75ad98d74358860d81b340cb957c2563a24f","source_sha256":"dacc0d7efaac95a92159dbdb346c641854f3314b4d26bc53c602bc54329ca4c1"},"harness_source_sha256":"dacc0d7efaac95a92159dbdb346c641854f3314b4d26bc53c602bc54329ca4c1","mechanical_pass_class":"native-stock-bound","mutant_count":12,"mutants_commitment_sha256":"313e47f59ba66b47d211f8b2e3c60ac89e454c09e233615e7c01f74539ca121f","mutants_passing_neutral_identity":12,"native_proof_sha256":"4334e6507f112b3665127177c2d5b1fe2c3606591e1c4f702ccd064176b9b022","plan_receipts":{"composite-ambient-bb66b923:SSGI":"dac0c0088c4a04870028823a94c7b454d26f6684db4a1dfd7050fba77e87bac8","composite-ambient-bb66b923:SSGI+Wetness":"755e6b390080ea28ac9d6f76351674845afcaadac5eb8308f9fc82fa68442b61","composite-ambient-bb66b923:Wetness":"59f4207f10be5ae602db08cc13f9b210e64b222a07bcfcfbbda143950b35d43b","composite-ambient-c36d04cc:SSGI":"3126cd9ad26f1d0e1030ffb1a8d04ff22691cc79e6bbb6187a17f8ec58f6636a","composite-ambient-c36d04cc:SSGI+Wetness":"b43fe748e9ec06a1eaa3f5f872516058c2b81931cb271a7f933d8575be0e6494","composite-ambient-c36d04cc:Wetness":"a61d98cdcb4e5ca7e1004bb3eece8f1581de33e75a8280c3842ff5b4af8ebd10"},"run_receipt_sha256":"2eaa0591443204d2bc2c4d45bba3bf4ddf83c1cb79a27b207437124cb8e9e364","semantic_commitments":{"checks_sha256":"02d8247840f86a6c7607d3067f2e22cb4e9df7b3cc230e1a2c21a395e8d5bd65","evidence_classes_sha256":"d6418bbe9ca4aedc3fafb0dff80a48bd59dd0d6f8591e461fdcb4d5d3b2456e8","mutants_sha256":"af54524b3418be5f2a818bed195835273ff0324fbe89f4f8407df5cdcf85b807","plan_receipts_sha256":"ec852dc91e8af38797025aa6a2b5f63c5e2636d792a235c34d7821aa794ae65a"}})json";
	inline constexpr std::string_view kExpectedCensus =
		R"json({"SSGI+Wetness_fail_blobs":30,"SSGI+Wetness_fail_occurrences":78,"SSGI_fail_blobs":30,"SSGI_fail_occurrences":78,"aggregate_declaration_commitment_sha256":"d088aa2cdea3d3ee36aa56fa00704ee8f0b5357ffbb81ef602994c85bca52c53","artifact_claims_commitment_sha256":"525f08359f6dab63c47d2d0290a78881358e774ad82037b0e234b4418a5d2d1d","occurrence_set_sha256":"60e41afcc1769629c25b0324e1c9040ed0d9550bb9db228fb2865640b1ebe961","static_mutation_count":38,"static_mutations_commitment_sha256":"3c7fbfb740e1d10dd3cf46ada09b2497ef0a4d5bfde31bf3e58d3bd29a43ff00","t0_blobs":30,"t0_occurrences":78,"t13_blobs":0,"t13_occurrences":0})json";

	// Exact scalar-kind census: any type substitution moves or adds a path.
	inline constexpr std::string_view kDoubleCensus =
		"artifact.byte_proof.native_proof.checks[].expected.ndotv=2\n"
		"artifact.byte_proof.native_proof.checks[].expected.tolerance=6\n"
		"artifact.byte_proof.native_proof.checks[].expected.wetness=2\n";
	inline constexpr std::string_view kNullCensus =
		"artifact.participants.SSGI.sampler=1\n"
		"artifact.participants.Wetness.sampler=1\n"
		"artifact.stock_identities[].outcomes[].patch_plan=306\n"
		"artifact.stock_identities[].stock_dcl_temps=2\n";
	inline constexpr std::string_view kBooleanCensus =
		"artifact.byte_proof.evidence_policy.source_recompiled_identity_grants_fidelity=1\n"
		"artifact.byte_proof.native_proof.checks[].passed=344\n"
		"artifact.byte_proof.native_proof.mutants[].neutral_identity_holds=12\n"
		"artifact.byte_proof.native_proof.mutants[].rejected=12\n"
		"artifact.byte_proof.native_proof.passed=1\n"
		"artifact.byte_proof.static_mutations[].rejected=38\n"
		"artifact.patch_plans[].fallback_proven=6\n"
		"artifact.patch_plans[].static_proof.passed=6\n"
		"artifact.release_policy.route_join_required=1\n"
		"artifact.release_policy.runtime_admissible=1\n";
}
