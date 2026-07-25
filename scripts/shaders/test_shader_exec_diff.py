#!/usr/bin/env python3

import json
import os
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


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-smoke"]:
        run_self_smoke()
    else:
        unittest.main()
