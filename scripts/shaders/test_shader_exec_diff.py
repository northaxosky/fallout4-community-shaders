#!/usr/bin/env python3

import json
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from collections import OrderedDict
from pathlib import Path

import shader_exec_diff as subject


class ShaderExecDiffUnitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contracts = json.loads(
            Path(subject.DEFAULT_CONTRACTS).read_text(encoding="utf-8")
        )
        cls.manifest = json.loads(
            Path(subject.DEFAULT_MANIFEST).read_text(encoding="utf-8")
        )
        cls.entries = {item["name"]: item for item in cls.manifest["shaders"]}
        cls.harness_source_sha256 = subject._hash_file(
            subject.HARNESS_SOURCE, include_sha1=False
        )["sha256"]
        cls.harness_executable = os.environ.get(
            "FO4CS_EXEC_HARNESS", subject.DEFAULT_EXECUTABLE
        )

    @classmethod
    def feature_measurement(cls, fixture):
        feature = cls.contracts["additive_features"]["wetness-effects"]
        wetness_format = next(
            item["wetness_format"]
            for item in feature["fixtures"]
            if item["id"] == fixture
        )
        requested_levels = feature["monotonic_levels"]
        uploaded_levels = [
            (
                int(value * 255.0 + 0.5) / 255.0
                if fixture == "native"
                else value
            )
            for value in requested_levels
        ]
        mask_payloads = subject._expected_wetness_mask_payloads(
            fixture, 16, 16, requested_levels
        )
        expected_buckets = subject._wetness_bucket_evidence_from_payload(
            fixture, mask_payloads["active-pattern"]
        )
        populations = {
            name: evidence["population"]
            for name, evidence in expected_buckets.items()
        }
        post_upload = {
            name: {
                "minimum": evidence["minimum"],
                "maximum": evidence["maximum"],
            }
            for name, evidence in expected_buckets.items()
        }
        cross_buckets = []
        for material in feature["material_buckets"]:
            for ibl in ("inactive", "active"):
                for mask in feature["mask_buckets"]:
                    population = populations[mask]
                    mrt = []
                    for name in feature["mrt_buckets"]:
                        zero_delta = mask == "mask.zero" or (
                            material == "material.skin" and name == "specular"
                        )
                        distribution = (
                            {
                                "min": 0.0,
                                "mean": 0.0,
                                "p50": 0.0,
                                "p95": 0.0,
                                "p99": 0.0,
                                "max": 0.0,
                            }
                            if zero_delta
                            else {
                                "min": 0.05,
                                "mean": 0.1,
                                "p50": 0.1,
                                "p95": 0.15,
                                "p99": 0.19,
                                "max": 0.2,
                            }
                        )
                        stock_energy = float(population * 3)
                        delta_energy = (
                            0.0 if zero_delta else population * 3 * 0.1
                        )
                        mrt.append(
                            {
                                "name": name,
                                "population": population,
                                "changed_pixels": 0 if zero_delta else population,
                                "changed_channels": (
                                    0 if zero_delta else population * 3
                                ),
                                "absolute_delta": distribution,
                                "stock_energy": stock_energy,
                                "delta_energy": delta_energy,
                                "denominator_threshold": max(
                                    1e-12, population * 3e-12
                                ),
                                "delta_fraction_of_stock": (
                                    delta_energy / stock_energy
                                ),
                                "energy_verdict": "PROVEN",
                            }
                        )
                    cross_buckets.append(
                        {
                            "fixture": fixture,
                            "mask": mask,
                            "material": material,
                            "ibl": ibl,
                            "population": population,
                            "post_upload": post_upload[mask],
                            "mrt": mrt,
                        }
                    )
        diffuse_series = []
        for material, ibl in (
            ("material.default", "inactive"),
            ("material.default", "active"),
            ("material.skin", "inactive"),
            ("material.skin", "active"),
        ):
            diffuse_series.append(
                {
                    "material": material,
                    "ibl": ibl,
                    "levels": [
                        {
                            "requested": requested,
                            "uploaded": uploaded,
                            "diffuse_delta_energy": float(index * 25),
                        }
                        for index, (requested, uploaded) in enumerate(
                            zip(requested_levels, uploaded_levels)
                        )
                    ],
                    "violations": 0,
                    "verdict": "PASS",
                }
            )
        mask_ids = [
            "active-pattern",
            "level-0",
            "level-1",
            "level-2",
            "level-3",
            "level-4",
            "neutral-zero",
        ]
        mask_hashes = [
            {
                "id": mask_id,
                "sha256": hashlib.sha256(
                    mask_payloads[mask_id]
                ).hexdigest(),
                "size": len(mask_payloads[mask_id]),
            }
            for mask_id in mask_ids
        ]
        return {
            "schema": subject.FEATURE_MEASUREMENT_SCHEMA,
            "schema_version": 1,
            "harness_version": subject.HARNESS_VERSION,
            "source_sha256": cls.harness_source_sha256,
            "evidence_class": "additive-feature",
            "comparison": "reconstructed-stock-vs-reconstructed-feature",
            "native_bytecode_used": False,
            "profile": feature["profile"],
            "fixture": fixture,
            "width": 16,
            "height": 16,
            "execution_environment": {
                "driver_type": "WARP",
                "feature_level": "11_0",
                "native_bytecode_used": False,
            },
            "measurement_protocol": feature["measurement_protocol"],
            "wetness_format": wetness_format,
            "measurement_format": "R32G32B32A32_FLOAT",
            "formats": [
                {
                    "bind_point": 4,
                    "dimension": "texture2d",
                    "resource_format": wetness_format,
                    "srv_format": wetness_format,
                }
            ],
            "seeds": [1, 2],
            "generated_inputs_sha256": "0" * 64,
            "hashes": {
                "uploaded_masks": [dict(item) for item in mask_hashes],
                "readback_masks": [dict(item) for item in mask_hashes],
            },
            "contract_delta": {
                "stock_to_feature": "only-texture2d-t4-added",
                "mutation_t4_optimization_away_allowed": True,
                "verdict": "PASS",
            },
            "variants": feature["variants"],
            "properties": {
                "neutral_identity": {
                    "tolerance_absolute": 0,
                    "tolerance_relative": 0,
                    "comparisons": 8,
                    "violations": [],
                    "verdict": "PASS",
                },
                "active_locality": {
                    "zero_tolerance": True,
                    "bucket_basis": (
                        "t4 GPU readback after fixture quantization; "
                        "material from post-quantization upload"
                    ),
                    "pattern": {
                        "exact_zero": True,
                        "exact_one": True,
                        "smooth_strict_partial_ramp": True,
                        "isolated_full_patch_with_zero_moat": True,
                    },
                    "violations": [],
                    "verdict": "PASS",
                },
                "magnitude": {
                    "rgb_only": True,
                    "invalid_denominator_buckets": 0,
                    "verdict": "PASS",
                },
                "monotonicity": {
                    "direct_specular_claim": "not-claimed",
                    "diffuse_series": diffuse_series,
                    "ibl_specular_probe": {
                        "scope": (
                            "controlled-positive-gradient-ambientSpecular"
                        ),
                        "levels": [
                            {
                                "requested": requested,
                                "uploaded": uploaded,
                                "delta_energy": float(index * 25),
                            }
                            for index, (requested, uploaded) in enumerate(
                                zip(requested_levels, uploaded_levels)
                            )
                        ],
                        "violations": 0,
                        "verdict": "PASS",
                    },
                    "violations": 0,
                    "verdict": "PASS",
                },
                "mutation_sensitivity": {
                    "id": "wetness-load-zero",
                    "expected_failed_property": "active_locality",
                    "observed_failed_properties": ["active_locality"],
                    "neutral_identity": "PASS",
                    "active_locality": "FAIL",
                    "verdict": "CAUGHT",
                },
            },
            "cross_buckets": cross_buckets,
            "verdict": "PASS",
        }

    def test_contract_schema_and_predicates(self):
        subject.validate_contracts(self.contracts)
        self.assertEqual(["adversarial", "native"], self.contracts["fixtures"])
        for profile in self.contracts["profiles"].values():
            for fixture in subject.FIXTURE_ORDER:
                required = profile["required_buckets"][fixture]
                self.assertEqual(len(required), len(set(required)))
                self.assertTrue(
                    all(profile["predicates"].get(name) for name in required)
                )

    def test_both_fixtures_and_required_mutations(self):
        self.assertEqual(
            {"adversarial", "native"}, set(self.contracts["fixtures"])
        )
        mutations = self.contracts["mutations"]
        self.assertEqual(len(subject.REQUIRED_MUTATION_IDS), len(mutations))
        self.assertEqual(
            subject.REQUIRED_MUTATION_IDS,
            {item["id"] for item in mutations},
        )
        self.assertTrue(all(item["class"] for item in mutations))
        directional = self.contracts["profiles"]["directional-lighting"]
        brdf_buckets = {
            "brdf.peak",
            "brdf.fallback",
            "brdf.unity-ratio",
            "brdf.nonunity-ratio",
        }
        for fixture in subject.FIXTURE_ORDER:
            self.assertTrue(
                brdf_buckets.issubset(
                    directional["required_buckets"][fixture]
                )
            )
        contract_profiles = {
            item["name"]: item["profile"]
            for item in self.contracts["contracts"]
        }
        self.assertEqual(
            "point-lighting-live",
            contract_profiles["bsdf_light_deferred_point"],
        )
        self.assertEqual(
            "ambient-ibl-runtime",
            contract_profiles["ambient_ibl_pass_runtime"],
        )
        self.assertEqual(
            "vls-slice-scatter",
            contract_profiles["vls_slice_scatter"],
        )
        for profile_name in (
            "ambient-ibl-runtime",
            "point-lighting-live",
            "vls-slice-scatter",
        ):
            required = self.contracts["profiles"][profile_name]["required_buckets"]
            self.assertIn("depth.equal", required["adversarial"])
            self.assertNotIn("depth.equal", required["native"])

    def test_additive_feature_contract_is_separate_and_exact(self):
        subject.validate_additive_features(
            self.contracts["additive_features"]
        )
        feature = self.contracts["additive_features"]["wetness-effects"]
        self.assertEqual(
            "reconstructed-stock-vs-reconstructed-feature",
            feature["comparison"],
        )
        self.assertFalse(feature["native_bytecode_used"])
        self.assertEqual(
            ["directional", "directional-ibl"],
            [item["id"] for item in feature["variants"]],
        )
        self.assertEqual(
            ["adversarial", "native"],
            [item["id"] for item in feature["fixtures"]],
        )
        self.assertEqual(10, len(self.contracts["mutations"]))
        self.assertEqual(
            subject.REQUIRED_MUTATION_IDS,
            {item["id"] for item in self.contracts["mutations"]},
        )

    def test_additive_feature_mutation_is_exact_and_nonmutating(self):
        feature = self.contracts["additive_features"]["wetness-effects"]
        source = os.path.join(subject.REPO_ROOT, feature["source"])
        before = Path(source).read_bytes()
        mutant = subject.build_additive_feature_mutant(
            before.decode("utf-8-sig"), feature
        )
        self.assertEqual(1, mutant.count(feature["mutation"]["new"]))
        self.assertNotIn(feature["mutation"]["old"], mutant)
        self.assertEqual(before, Path(source).read_bytes())
        with self.assertRaises(subject.StableFailure):
            subject.build_additive_feature_mutant(
                before.decode("utf-8-sig").replace(
                    feature["mutation"]["old"],
                    feature["mutation"]["old"] * 2,
                ),
                feature,
            )

    def test_additive_feature_measurement_schema_and_matrix(self):
        feature = self.contracts["additive_features"]["wetness-effects"]
        for fixture in ("adversarial", "native"):
            measurement = self.feature_measurement(fixture)
            self.assertEqual(
                [],
                subject.validate_feature_measurement(
                    measurement,
                    fixture,
                    feature,
                    16,
                    16,
                    self.harness_source_sha256,
                ),
            )
            keys = {
                (item["mask"], item["material"], item["ibl"])
                for item in measurement["cross_buckets"]
            }
            self.assertEqual(12, len(keys))
            self.assertTrue(
                all(
                    [mrt["name"] for mrt in item["mrt"]]
                    == ["diffuse", "specular"]
                    and item["post_upload"]
                    for item in measurement["cross_buckets"]
                )
            )
        malformed = self.feature_measurement("adversarial")
        malformed["properties"]["mutation_sensitivity"]["verdict"] = "MISSED"
        self.assertTrue(
            subject.validate_feature_measurement(
                malformed,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        malformed_mrt = self.feature_measurement("adversarial")
        malformed_mrt["cross_buckets"][0]["mrt"].append(None)
        self.assertTrue(
            subject.validate_feature_measurement(
                malformed_mrt,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        zeroed_active = self.feature_measurement("adversarial")
        active_bucket = next(
            item
            for item in zeroed_active["cross_buckets"]
            if item["mask"] == "mask.partial"
            and item["material"] == "material.default"
            and item["ibl"] == "inactive"
        )
        for stats in active_bucket["mrt"]:
            stats["changed_pixels"] = 0
            stats["changed_channels"] = 0
            stats["delta_energy"] = 0.0
            stats["delta_fraction_of_stock"] = 0.0
            stats["absolute_delta"] = {
                name: 0.0
                for name in ("min", "mean", "p50", "p95", "p99", "max")
            }
        self.assertTrue(
            subject.validate_feature_measurement(
                zeroed_active,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        missing_series = self.feature_measurement("adversarial")
        missing_series["properties"]["monotonicity"]["diffuse_series"].pop()
        self.assertTrue(
            subject.validate_feature_measurement(
                missing_series,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        missing_hash_id = self.feature_measurement("native")
        missing_hash_id["hashes"]["uploaded_masks"].pop(2)
        missing_hash_id["hashes"]["readback_masks"].pop(2)
        self.assertTrue(
            subject.validate_feature_measurement(
                missing_hash_id,
                "native",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        mismatched_hash = self.feature_measurement("native")
        mismatched_hash["hashes"]["readback_masks"][0]["sha256"] = "f" * 64
        self.assertTrue(
            subject.validate_feature_measurement(
                mismatched_hash,
                "native",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        repeated_hash = self.feature_measurement("native")
        repeated = repeated_hash["hashes"]["uploaded_masks"][0]["sha256"]
        for name in ("uploaded_masks", "readback_masks"):
            for item in repeated_hash["hashes"][name]:
                item["sha256"] = repeated
        self.assertTrue(
            subject.validate_feature_measurement(
                repeated_hash,
                "native",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        different_split = self.feature_measurement("adversarial")
        original_populations = {
            item["mask"]: item["population"]
            for item in different_split["cross_buckets"]
            if item["material"] == "material.default"
            and item["ibl"] == "inactive"
        }
        replacement_populations = {
            "mask.zero": original_populations["mask.zero"] - 1,
            "mask.partial": original_populations["mask.partial"] + 1,
            "mask.full": original_populations["mask.full"],
        }
        partial_record = next(
            item
            for item in different_split["cross_buckets"]
            if item["mask"] == "mask.partial"
        )
        replacement_partial_range = {
            "minimum": partial_record["post_upload"]["minimum"] / 2.0,
            "maximum": (
                1.0 + partial_record["post_upload"]["maximum"]
            )
            / 2.0,
        }
        for bucket in different_split["cross_buckets"]:
            population = replacement_populations[bucket["mask"]]
            bucket["population"] = population
            if bucket["mask"] == "mask.partial":
                bucket["post_upload"] = replacement_partial_range
            for stats in bucket["mrt"]:
                zero_delta = bucket["mask"] == "mask.zero" or (
                    bucket["material"] == "material.skin"
                    and stats["name"] == "specular"
                )
                stats["population"] = population
                stats["stock_energy"] = float(population * 3)
                stats["denominator_threshold"] = max(
                    1e-12, population * 3e-12
                )
                if zero_delta:
                    stats["changed_pixels"] = 0
                    stats["changed_channels"] = 0
                    stats["delta_energy"] = 0.0
                    stats["delta_fraction_of_stock"] = 0.0
                else:
                    stats["changed_pixels"] = population
                    stats["changed_channels"] = population * 3
                    stats["delta_energy"] = population * 3 * 0.1
                    stats["delta_fraction_of_stock"] = 0.1
        self.assertTrue(
            subject.validate_feature_measurement(
                different_split,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        zero_monotonic = self.feature_measurement("adversarial")
        for series in zero_monotonic["properties"]["monotonicity"][
            "diffuse_series"
        ]:
            for level in series["levels"]:
                level["diffuse_delta_energy"] = 0.0
        self.assertTrue(
            subject.validate_feature_measurement(
                zero_monotonic,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        contradictory_channels = self.feature_measurement("adversarial")
        diffuse = next(
            item
            for item in contradictory_channels["cross_buckets"]
            if item["mask"] == "mask.partial"
            and item["material"] == "material.default"
            and item["ibl"] == "inactive"
        )["mrt"][0]
        diffuse["changed_channels"] = diffuse["population"]
        self.assertTrue(
            subject.validate_feature_measurement(
                contradictory_channels,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        summary = subject._feature_property_summary(
            [{"fixture": "native"}], "neutral_identity"
        )
        self.assertEqual("UNPROVEN", summary["verdict"])

    def test_additive_feature_manifest_is_canonical_and_path_safe(self):
        report = OrderedDict(
            (
                ("schema", subject.FEATURE_RUN_SCHEMA),
                ("schema_version", 1),
                ("evidence_class", "additive-feature"),
                (
                    "comparison",
                    "reconstructed-stock-vs-reconstructed-feature",
                ),
                ("native_bytecode_used", False),
                (
                    "fixtures",
                    [
                        self.feature_measurement("adversarial"),
                        self.feature_measurement("native"),
                    ],
                ),
                ("verdict", "PASS"),
            )
        )
        subject._assert_receipt_safe(report)
        first = subject._canonical_bytes(report)
        second = subject._canonical_bytes(json.loads(first))
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertNotIn(b"corpus_sha", first)
        self.assertNotIn(b"native_bytecode_hash", first)
        self.assertNotIn(b"DXBC", first)
        with self.assertRaises(subject.StableFailure):
            subject._assert_receipt_safe(
                {"tool": r"C:\absolute\fxc.exe"}
            )
        with self.assertRaises(subject.StableFailure):
            subject._assert_receipt_safe({"tool": "/tmp"})

    def test_feature_source_snapshot_and_staleness_detection(self):
        feature = self.contracts["additive_features"]["wetness-effects"]
        source = os.path.join(subject.REPO_ROOT, feature["source"])
        report, captures = subject._capture_hlsl_closure(source)
        self.assertGreaterEqual(len(captures), 2)
        self.assertRegex(report["combined_sha256"], r"^[0-9a-f]{64}$")
        with tempfile.TemporaryDirectory() as temporary:
            snapshot = subject._write_hlsl_snapshot(
                temporary, captures, feature["source"]
            )
            self.assertEqual(
                next(
                    item["data"]
                    for item in captures
                    if item["label"] == feature["source"]
                ),
                Path(snapshot).read_bytes(),
            )
            snapshot_capture = subject._capture_file(
                snapshot, f"snapshot/{feature['source']}"
            )
            self.assertEqual(
                [], subject._verify_captured_files([snapshot_capture])
            )
            Path(snapshot).write_bytes(Path(snapshot).read_bytes() + b"\n")
            self.assertEqual(
                [f"snapshot/{feature['source']}"],
                subject._verify_captured_files([snapshot_capture]),
            )
            probe = os.path.join(temporary, "probe.bin")
            Path(probe).write_bytes(b"before")
            capture = subject._capture_file(probe, "probe.bin")
            self.assertEqual([], subject._verify_captured_files([capture]))
            Path(probe).write_bytes(b"after")
            self.assertEqual(
                ["probe.bin"], subject._verify_captured_files([capture])
            )

    def test_native_cli_defaults_are_unchanged(self):
        arguments = subject._parse_arguments([])
        self.assertIsNone(arguments.additive_feature)
        self.assertEqual("all", arguments.fixture)
        self.assertEqual("required", arguments.mutation_suite)
        self.assertEqual(subject.REPORT_SCHEMA, "fo4cs.shader-exec-diff-report")
        self.assertEqual(
            subject.MEASUREMENT_SCHEMA,
            "fo4cs.shader-exec-measurement",
        )
        native_only_contracts = json.loads(json.dumps(self.contracts))
        native_only_contracts.pop("additive_features")
        subject.validate_contracts(native_only_contracts)

    def test_source_transforms_are_exact_and_nonmutating(self):
        before = {}
        for mutation in self.contracts["mutations"]:
            entry = self.entries[mutation["target"]]
            path = os.path.join(subject.REPO_ROOT, entry["src"])
            original_bytes = Path(path).read_bytes()
            before[path] = original_bytes
            control, mutant = subject.build_mutation_sources(
                original_bytes.decode("utf-8-sig"),
                mutation["target"],
                mutation["id"],
            )
            self.assertNotEqual(control, mutant)
            self.assertEqual(original_bytes, Path(path).read_bytes())
        for path, original_bytes in before.items():
            self.assertEqual(original_bytes, Path(path).read_bytes())

    def test_replace_exact_rejects_ambiguous_transform(self):
        with self.assertRaises(subject.StableFailure):
            subject._replace_exact("x x", "x", "y", "ambiguous")

    def test_canonical_order_and_final_lf(self):
        value = OrderedDict(
            (
                ("schema", subject.REPORT_SCHEMA),
                ("schema_version", 1),
                ("producer", OrderedDict((("name", "test"),))),
                ("verdict", "PASS"),
            )
        )
        encoded = subject._canonical_bytes(value)
        self.assertTrue(encoded.endswith(b"\n"))
        self.assertFalse(encoded.endswith(b"\n\n"))
        self.assertTrue(encoded.startswith(b'{"schema":'))
        self.assertEqual(encoded, subject._canonical_bytes(json.loads(encoded)))

    def test_report_rejects_paths_and_contains_no_native_content(self):
        safe = OrderedDict(
            (
                ("schema", subject.REPORT_SCHEMA),
                ("schema_version", 1),
                ("source", "shaders/lighting/ambient_ibl_pass.hlsl"),
                (
                    "native",
                    OrderedDict(
                        (
                            ("sha1", "0" * 40),
                            ("sha256", "0" * 64),
                            ("size", 12),
                        )
                    ),
                ),
            )
        )
        subject._assert_receipt_safe(safe)
        encoded = subject._canonical_bytes(safe)
        self.assertNotIn(b"DXBC", encoded)
        with self.assertRaises(subject.StableFailure):
            subject._assert_receipt_safe({"path": r"C:\game\shader.dxbc"})

    def test_staleness_is_byte_exact(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = os.path.join(temporary, "report.json")
            current = subject._canonical_bytes(
                OrderedDict((("schema", subject.REPORT_SCHEMA), ("verdict", "PASS")))
            )
            Path(path).write_bytes(current)
            self.assertTrue(subject.check_report_bytes(path, current))
            changed = subject._canonical_bytes(
                OrderedDict((("schema", subject.REPORT_SCHEMA), ("verdict", "FAIL")))
            )
            self.assertFalse(subject.check_report_bytes(path, changed))
            Path(path).write_bytes(current.rstrip())
            self.assertFalse(subject.check_report_bytes(path, current))

    def test_structured_result_classification(self):
        zero = subject._measurement_metrics_zero()
        mutations = [
            {"verdict": "CAUGHT"}
            for _ in subject.REQUIRED_MUTATION_IDS
        ]
        self.assertEqual(
            ("PASS", 0),
            subject.classify_result(True, True, [], zero, mutations),
        )
        divergent = dict(zero)
        divergent["divergent_pixels"] = 1
        self.assertEqual(
            ("FAIL", 1),
            subject.classify_result(True, True, [], divergent, mutations),
        )
        tolerance = subject._regular_tolerance_failure(
            {"aggregate": {"divergent_pixels": 7}},
            [],
            "target",
            "native",
        )
        self.assertEqual(
            {"code": "tolerance_breach", "detail": "target:native"},
            tolerance,
        )
        self.assertEqual(
            ("FAIL", 1),
            subject.classify_result(
                True, True, [tolerance], divergent, mutations
            ),
        )
        self.assertEqual(
            ("UNPROVEN", 2),
            subject.classify_result(
                True,
                True,
                [
                    tolerance,
                    {"code": "measurement_protocol", "detail": "x"},
                ],
                divergent,
                mutations,
            ),
        )
        self.assertEqual(
            ("UNPROVEN", 2),
            subject.classify_result(False, True, [], zero, mutations),
        )
        self.assertEqual(
            ("UNPROVEN", 2),
            subject.classify_result(
                True,
                True,
                [{"code": "missing_bucket", "detail": "depth.equal"}],
                zero,
                mutations,
            ),
        )
        missed = mutations[:-1] + [{"verdict": "MISSED"}]
        self.assertEqual(
            ("FAIL", 1),
            subject.classify_result(True, True, [], zero, missed),
        )
        unproven = mutations[:-1] + [{"verdict": "UNPROVEN"}]
        self.assertEqual(
            ("UNPROVEN", 2),
            subject.classify_result(True, True, [], zero, unproven),
        )
        for state in (
            "mutation_compile_failed",
            "mutation_transform",
            "mutation_protocol",
            "mutation_control_dirty",
        ):
            infrastructure = subject._mutation_result(
                self.contracts["mutations"][0],
                None,
                None,
                64,
                state,
            )
            self.assertEqual("UNPROVEN", infrastructure["verdict"])
            self.assertEqual(
                ("UNPROVEN", 2),
                subject.classify_result(
                    True,
                    True,
                    [{"code": state, "detail": "x"}],
                    zero,
                    [infrastructure],
                ),
            )
        clean_control = {"aggregate": {"divergent_pixels": 0}}
        clean_mutant = {
            "buckets": [
                {
                    "name": self.contracts["mutations"][0]["expected_bucket"],
                    "population": 64,
                    "divergent_pixels": 0,
                }
            ]
        }
        self.assertEqual(
            "MISSED",
            subject._mutation_result(
                self.contracts["mutations"][0],
                clean_control,
                clean_mutant,
                64,
            )["verdict"],
        )

    def test_focused_scope_uses_only_relevant_mutations(self):
        all_targets = {item["name"] for item in self.contracts["contracts"]}
        selected = {"ambient_ibl_pass"}
        self.assertEqual("focused", subject._report_scope(selected, all_targets))
        relevant = subject._relevant_mutations(
            self.contracts["mutations"], selected
        )
        self.assertTrue(relevant)
        self.assertEqual(selected, {item["target"] for item in relevant})
        caught = [{"verdict": "CAUGHT"} for _ in relevant]
        self.assertEqual(
            ("PASS", 0),
            subject.classify_result(
                True, True, [], subject._measurement_metrics_zero(), caught
            ),
        )
        with self.assertRaises(subject.StableFailure):
            subject._validate_scope_artifacts("focused", "out", None)
        subject._validate_scope_artifacts("focused", None, None)

    def test_harness_self_test_when_built(self):
        if not os.path.isfile(self.harness_executable):
            if os.environ.get("FO4CS_REQUIRE_EXEC_HARNESS") == "1":
                self.fail("required shader execution harness is not built")
            self.skipTest("standalone harness is not built")
        result = subject._run_harness_self_test(
            self.harness_executable, self.harness_source_sha256
        )
        self.assertEqual("PASS", result["tests"]["binary16"])
        self.assertEqual("PASS", result["tests"]["dimension_hash"])
        self.assertEqual("PASS", result["tests"]["front_face_probe"])
        self.assertNotEqual(
            result["front_face_probe"]["clockwise_state_front"],
            result["front_face_probe"]["counter_clockwise_state_front"],
        )
        with self.assertRaises(subject.StableFailure) as context:
            subject._run_harness_self_test(
                self.harness_executable, "0" * 64
            )
        self.assertEqual("stale_harness", context.exception.code)

    def test_feature_failure_receipt_when_built(self):
        if not os.path.isfile(self.harness_executable):
            if os.environ.get("FO4CS_REQUIRE_EXEC_HARNESS") == "1":
                self.fail("required shader execution harness is not built")
            self.skipTest("standalone harness is not built")
        with tempfile.TemporaryDirectory() as temporary:
            missing = os.path.join(temporary, "missing.dxbc")
            receipt_path = os.path.join(temporary, "failure.json")
            process = subprocess.run(
                [
                    self.harness_executable,
                    missing,
                    missing,
                    "--wetness-feature",
                    "--stock-ibl",
                    missing,
                    "--feature-ibl",
                    missing,
                    "--mutant",
                    missing,
                    "--mutant-ibl",
                    missing,
                    "--measurement-json",
                    receipt_path,
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, process.returncode)
            receipt = json.loads(Path(receipt_path).read_text(encoding="utf-8"))
            self.assertEqual(
                subject.FEATURE_MEASUREMENT_SCHEMA, receipt["schema"]
            )
            self.assertEqual("additive-feature", receipt["evidence_class"])
            self.assertEqual(
                "reconstructed-stock-vs-reconstructed-feature",
                receipt["comparison"],
            )
            self.assertFalse(receipt["native_bytecode_used"])
            self.assertEqual("UNPROVEN", receipt["verdict"])
            self.assertNotIn("aggregate", receipt)
            feature = self.contracts["additive_features"]["wetness-effects"]
            validation = subject.validate_feature_measurement(
                receipt,
                "adversarial",
                feature,
                256,
                256,
                self.harness_source_sha256,
            )
            self.assertTrue(validation)
            self.assertFalse(
                any(
                    item["code"] == "feature_measurement_protocol"
                    for item in validation
                )
            )

    def test_feature_driver_failure_writes_additive_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = os.path.join(temporary, "failure.json")
            report, exit_code = subject.run(
                [
                    "--additive-feature",
                    "wetness-effects",
                    "--exe",
                    os.path.join(temporary, "missing.exe"),
                    "--json",
                    output,
                ]
            )
            self.assertEqual(2, exit_code)
            self.assertEqual("UNPROVEN", report["verdict"])
            self.assertEqual(subject.FEATURE_RUN_SCHEMA, report["schema"])
            self.assertEqual(
                subject._canonical_bytes(report), Path(output).read_bytes()
            )
            self.assertEqual(
                ["adversarial", "native"],
                [item["fixture"] for item in report["fixtures"]],
            )
            for receipt in report["fixtures"]:
                self.assertEqual(
                    subject.FEATURE_MEASUREMENT_SCHEMA, receipt["schema"]
                )
                self.assertEqual(
                    "additive-feature", receipt["evidence_class"]
                )
                self.assertEqual(
                    "reconstructed-stock-vs-reconstructed-feature",
                    receipt["comparison"],
                )
                self.assertFalse(receipt["native_bytecode_used"])
                self.assertEqual("UNPROVEN", receipt["verdict"])
                self.assertNotIn("aggregate", receipt)
            for failure in report["failures"]:
                self.assertEqual(
                    subject.FEATURE_MEASUREMENT_SCHEMA, failure["schema"]
                )
                self.assertEqual("additive-feature", failure["evidence_class"])
                self.assertFalse(failure["native_bytecode_used"])

    def test_runtime_component_provenance_is_safe_and_sorted(self):
        if os.path.isfile(self.harness_executable):
            components = subject._run_harness_self_test(
                self.harness_executable, self.harness_source_sha256
            )["runtime_components"]
        else:
            components = [
                {
                    "name": "d3d10warp.dll",
                    "state": "available",
                    "version": "1.0.0.0",
                    "sha256": "0" * 64,
                    "size": 1,
                },
                {
                    "name": "d3d11.dll",
                    "state": "available",
                    "version": "1.0.0.0",
                    "sha256": "1" * 64,
                    "size": 1,
                },
            ]
        subject._validate_runtime_components(components)
        self.assertEqual(
            ["d3d10warp.dll", "d3d11.dll"],
            [item["name"] for item in components],
        )
        for component in components:
            self.assertIn(component["state"], {"available", "unavailable"})
            self.assertNotIn("path", component)
            if component["state"] == "available":
                self.assertRegex(component["sha256"], r"^[0-9a-f]{64}$")
                self.assertGreater(component["size"], 0)
                self.assertTrue(component["version"])
        subject._assert_receipt_safe({"components": components})
        unavailable = [dict(item) for item in components]
        unavailable[0] = {
            "name": "d3d10warp.dll",
            "state": "unavailable",
        }
        with self.assertRaises(subject.StableFailure) as unavailable_context:
            subject._validate_runtime_components(unavailable)
        self.assertEqual("runtime_identity", unavailable_context.exception.code)
        malformed = [dict(item) for item in components]
        malformed[1]["sha256"] = "not-a-hash"
        with self.assertRaises(subject.StableFailure) as malformed_context:
            subject._validate_runtime_components(malformed)
        self.assertEqual("runtime_identity", malformed_context.exception.code)
        unknown_version = [dict(item) for item in components]
        unknown_version[1]["version"] = "unknown"
        with self.assertRaises(subject.StableFailure) as version_context:
            subject._validate_runtime_components(unknown_version)
        self.assertEqual("runtime_identity", version_context.exception.code)

    def test_measurement_validation_is_structured(self):
        metrics = subject._measurement_metrics_zero()
        metrics["total_pixels"] = 64
        metrics["total_channels"] = 256
        measurement = {
            "schema": subject.MEASUREMENT_SCHEMA,
            "schema_version": 1,
            "harness_version": subject.HARNESS_VERSION,
            "source_sha256": self.harness_source_sha256,
            "profile": "ambient-ibl",
            "fixture": "adversarial",
            "width": 16,
            "height": 16,
            "execution_environment": {
                "driver_type": "WARP",
                "feature_level": "11_0",
                "runtime_fingerprint": "system-d3d11-runtime",
                "limitation": "runtime identity external",
            },
            "measurement_format": "R32G32B32A32_FLOAT",
            "generated_inputs_sha256": "0" * 64,
            "seed_base": 0,
            "seeds": [0],
            "scenario_seeds": [0, 1],
            "formats": [{"bind_point": 1}],
            "matrix_assertions": {"verdict": "PASS"},
            "aggregate": metrics,
            "buckets": [
                {
                    "name": "depth.equal",
                    "population": 64,
                    "required_minimum": 64,
                    **metrics,
                },
            ],
            "failures": [],
            "verdict": "PASS",
        }
        self.assertEqual(
            [],
            subject.validate_measurement(
                measurement,
                "ambient-ibl",
                "adversarial",
                ["depth.equal"],
                64,
                16,
                16,
                self.harness_source_sha256,
            ),
        )
        failures = subject.validate_measurement(
            measurement,
            "ambient-ibl",
            "adversarial",
            ["depth.equal", "ibl.on"],
            64,
            16,
            16,
            self.harness_source_sha256,
        )
        self.assertEqual("missing_bucket", failures[0]["code"])
        sparse = {
            "schema": subject.MEASUREMENT_SCHEMA,
            "schema_version": 1,
            "profile": "ambient-ibl",
            "fixture": "adversarial",
            "buckets": [],
        }
        self.assertTrue(
            subject.validate_measurement(
                sparse,
                "ambient-ibl",
                "adversarial",
                [],
                64,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        malformed = dict(measurement)
        malformed["failures"] = None
        result = subject.validate_harness_result(
            malformed,
            0,
            "ambient-ibl",
            "adversarial",
            ["depth.equal"],
            64,
            "x",
            16,
            16,
            self.harness_source_sha256,
        )
        self.assertTrue(
            any(item["code"] == "measurement_protocol" for item in result)
        )
        empty = dict(measurement)
        empty["aggregate"] = subject._measurement_metrics_zero()
        self.assertTrue(
            subject.validate_measurement(
                empty,
                "ambient-ibl",
                "adversarial",
                ["depth.equal"],
                64,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        stale = dict(measurement)
        stale["source_sha256"] = "f" * 64
        self.assertTrue(
            any(
                item["code"] == "stale_harness"
                for item in subject.validate_measurement(
                    stale,
                    "ambient-ibl",
                    "adversarial",
                    ["depth.equal"],
                    64,
                    16,
                    16,
                    self.harness_source_sha256,
                )
            )
        )
        wrong_dimensions = dict(measurement)
        self.assertTrue(
            subject.validate_measurement(
                wrong_dimensions,
                "ambient-ibl",
                "adversarial",
                ["depth.equal"],
                64,
                17,
                16,
                self.harness_source_sha256,
            )
        )


def run_self_smoke():
    executable = os.environ.get(
        "FO4CS_EXEC_HARNESS", subject.DEFAULT_EXECUTABLE
    )
    source_sha256 = subject._hash_file(
        subject.HARNESS_SOURCE, include_sha1=False
    )["sha256"]
    subject._run_harness_self_test(executable, source_sha256)
    fxc = subject._find_fxc(None)
    contracts = json.loads(
        Path(subject.DEFAULT_CONTRACTS).read_text(encoding="utf-8")
    )
    manifest = json.loads(
        Path(subject.DEFAULT_MANIFEST).read_text(encoding="utf-8")
    )
    entries = {item["name"]: item for item in manifest["shaders"]}
    contract_by_name = {
        item["name"]: item for item in contracts["contracts"]
    }
    with tempfile.TemporaryDirectory() as temporary:
        for contract in contracts["contracts"]:
            entry = entries[contract["name"]]
            source = os.path.join(subject.REPO_ROOT, entry["src"])
            bytecode = os.path.join(
                temporary, f"{contract['name']}.dxbc"
            )
            compiled, log = subject._compile_shader(
                fxc,
                source,
                os.path.dirname(source),
                entry.get("defines", []),
                bytecode,
            )
            if not compiled:
                raise AssertionError(log)
            for fixture in subject.FIXTURE_ORDER:
                required = contracts["profiles"][contract["profile"]][
                    "required_buckets"
                ][fixture]
                measurement, return_code, human = subject._run_harness(
                    executable,
                    bytecode,
                    bytecode,
                    fixture,
                    contract["profile"],
                    required,
                    contracts["minimum_bucket_population"],
                    1,
                    31,
                    16,
                    16,
                    2e-3,
                    1e-2,
                    temporary,
                    f"{contract['name']}-{fixture}",
                    False,
                )
                failures = subject.validate_harness_result(
                    measurement,
                    return_code,
                    contract["profile"],
                    fixture,
                    required,
                    contracts["minimum_bucket_population"],
                    contract["name"],
                    16,
                    16,
                    source_sha256,
                )
                if return_code != 0 or failures:
                    raise AssertionError((failures, human))
                print(f"{contract['name']} {fixture}: PASS")

        for mutation in contracts["mutations"]:
            contract = contract_by_name[mutation["target"]]
            entry = entries[mutation["target"]]
            source = os.path.join(subject.REPO_ROOT, entry["src"])
            original = Path(source).read_text(encoding="utf-8-sig")
            control, mutant = subject.build_mutation_sources(
                original, mutation["target"], mutation["id"]
            )
            bytecodes = []
            for kind, text in (("control", control), ("mutant", mutant)):
                hlsl = os.path.join(
                    temporary, f"{mutation['id']}-{kind}.hlsl"
                )
                bytecode = os.path.join(
                    temporary, f"{mutation['id']}-{kind}.dxbc"
                )
                Path(hlsl).write_text(text, encoding="utf-8", newline="\n")
                compiled, log = subject._compile_shader(
                    fxc,
                    hlsl,
                    os.path.dirname(source),
                    entry.get("defines", []),
                    bytecode,
                )
                if not compiled:
                    raise AssertionError(log)
                bytecodes.append(bytecode)
            required = contracts["profiles"][contract["profile"]][
                "required_buckets"
            ][mutation["fixture"]]
            measurement, return_code, human = subject._run_harness(
                executable,
                bytecodes[0],
                bytecodes[1],
                mutation["fixture"],
                contract["profile"],
                required,
                contracts["minimum_bucket_population"],
                1,
                31,
                16,
                16,
                2e-3,
                1e-2,
                temporary,
                mutation["id"],
                False,
            )
            failures = subject.validate_harness_result(
                measurement,
                return_code,
                contract["profile"],
                mutation["fixture"],
                required,
                contracts["minimum_bucket_population"],
                mutation["id"],
                16,
                16,
                source_sha256,
            )
            bucket = next(
                item
                for item in measurement["buckets"]
                if item["name"] == mutation["expected_bucket"]
            )
            if (
                return_code != 1
                or failures
                or bucket["divergent_pixels"] == 0
                or (
                    mutation["class"] == "boundary-operator"
                    and any(
                        item["divergent_pixels"] != 0
                        for item in measurement["buckets"]
                        if item["name"] in {"depth.near", "depth.far"}
                    )
                )
            ):
                raise AssertionError((failures, bucket, human))
            print(f"{mutation['id']}: CAUGHT")


def run_wetness_smoke():
    executable = os.environ.get(
        "FO4CS_EXEC_HARNESS", subject.DEFAULT_EXECUTABLE
    )
    if not os.path.isfile(executable):
        raise AssertionError("required shader execution harness is not built")
    fxc = subject._find_fxc(None)
    with tempfile.TemporaryDirectory() as first_directory:
        with tempfile.TemporaryDirectory() as second_directory:
            arguments = [
                "--additive-feature",
                "wetness-effects",
                "--exe",
                executable,
                "--fxc",
                fxc,
                "--width",
                "16",
                "--height",
                "16",
                "--seeds",
                "1",
                "--seed-base",
                "31",
                "--manifest-dir",
                first_directory,
            ]
            first_report, first_code = subject.run(arguments)
            arguments[-1] = second_directory
            second_report, second_code = subject.run(arguments)
            if first_code != 0 or second_code != 0:
                raise AssertionError((first_report, second_report))
            first_path = os.path.join(
                first_directory, subject.FEATURE_MANIFEST_FILENAME
            )
            second_path = os.path.join(
                second_directory, subject.FEATURE_MANIFEST_FILENAME
            )
            if Path(first_path).read_bytes() != Path(second_path).read_bytes():
                raise AssertionError("WetnessEffects manifest is not deterministic")
            if first_report != second_report:
                raise AssertionError("WetnessEffects report is not deterministic")
            if (
                first_report["schema"] != subject.FEATURE_RUN_SCHEMA
                or first_report["evidence_class"] != "additive-feature"
                or first_report["comparison"]
                != "reconstructed-stock-vs-reconstructed-feature"
                or first_report["native_bytecode_used"] is not False
                or first_report["verdict"] != "PASS"
            ):
                raise AssertionError(first_report)
            fixtures = first_report["fixtures"]
            if [item["fixture"] for item in fixtures] != [
                "adversarial",
                "native",
            ]:
                raise AssertionError(fixtures)
            for fixture in fixtures:
                if (
                    len(fixture["cross_buckets"]) != 12
                    or fixture["properties"]["neutral_identity"]["verdict"]
                    != "PASS"
                    or fixture["properties"]["active_locality"]["verdict"]
                    != "PASS"
                    or fixture["properties"]["magnitude"]["verdict"]
                    != "PASS"
                    or fixture["properties"]["monotonicity"]["verdict"]
                    != "PASS"
                    or fixture["properties"]["mutation_sensitivity"]["verdict"]
                    != "CAUGHT"
                ):
                    raise AssertionError(fixture)
            print("WetnessEffects additive-feature WARP: PASS")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-smoke"]:
        run_self_smoke()
    elif sys.argv[1:] == ["--wetness-smoke"]:
        run_wetness_smoke()
    else:
        unittest.main()
