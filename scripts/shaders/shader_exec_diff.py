#!/usr/bin/env python3
"""Execute reconstructed and corpus shaders on deterministic WARP fixtures."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
from collections import OrderedDict
from pathlib import Path
from typing import Any, Iterable

from shader_corpus_diff import (
    DEFAULT_CORPUS_DIR,
    DEFAULT_FXC,
    DEFAULT_MANIFEST,
    REPO_ROOT,
)

TOOL_VERSION = 4
HARNESS_VERSION = 4
REPORT_SCHEMA = "fo4cs.shader-exec-diff-report"
RUN_SCHEMA = "fo4cs.shader-exec-diff-run"
MEASUREMENT_SCHEMA = "fo4cs.shader-exec-measurement"
FEATURE_RUN_SCHEMA = "fo4cs.shader-additive-feature-run"
FEATURE_MEASUREMENT_SCHEMA = "fo4cs.shader-additive-feature-measurement"
CONTRACTS_SCHEMA = "fo4cs.shader-exec-contracts"
DEFAULT_CONTRACTS = os.path.join(
    REPO_ROOT, "scripts", "shaders", "shader-exec-contracts.json"
)
DEFAULT_EXECUTABLE = os.path.join(REPO_ROOT, ".shader-cache", "shader_exec_diff.exe")
HARNESS_SOURCE = os.path.join(
    REPO_ROOT, "scripts", "shaders", "shader_exec_diff", "main.cpp"
)
MANIFEST_FILENAME = "shader-exec-diff-run.json"
FEATURE_MANIFEST_FILENAME = "wetness-effects-warp-run.json"
FEATURE_PROTOCOL_VERSION = 5
COMPILE_FLAGS = ("/nologo", "/T", "ps_5_0", "/O3", "/E", "main")
FIXTURE_ORDER = ("adversarial", "native")
VERDICTS = ("PASS", "FAIL", "UNPROVEN", "STALE")
REQUIRED_MUTATION_IDS = {
    "depth-exclusive-boundary",
    "transposed-reprojection",
    "missing-axis-zyx",
    "flipped-view-direction",
    "omitted-t12-sample",
    "wrong-texture-slot",
    "omitted-material-branch",
    "point-depth-exclusive-boundary",
    "runtime-depth-exclusive-boundary",
    "vls-depth-exclusive-boundary",
}


class StableFailure(Exception):
    def __init__(self, code: str, detail: str, exit_code: int = 2):
        super().__init__(detail)
        self.code = code
        self.detail = detail
        self.exit_code = exit_code


def _positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def _nonnegative_int(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return parsed


def _nonnegative_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return parsed


def _find_fxc(explicit: str | None) -> str:
    for candidate in (explicit, os.environ.get("FXC_PATH"), DEFAULT_FXC):
        if candidate and os.path.isfile(candidate):
            return os.path.abspath(candidate)
    raise StableFailure("compiler_missing", "fxc.exe was not found")


def _run_harness_self_test(
    executable: str, expected_source_sha256: str
) -> dict[str, Any]:
    process = subprocess.run(
        [executable, "--self-test"],
        capture_output=True,
        text=True,
    )
    try:
        result = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise StableFailure("harness_self_test", "invalid self-test protocol") from error
    if (
        process.returncode != 0
        or result.get("schema") != "fo4cs.shader-exec-self-test"
        or result.get("schema_version") != 1
        or result.get("harness_version") != HARNESS_VERSION
        or result.get("verdict") != "PASS"
    ):
        raise StableFailure("harness_self_test", "self-test failed")
    if result.get("source_sha256") != expected_source_sha256:
        raise StableFailure("stale_harness", "source_sha256")
    tests = result.get("tests")
    if not isinstance(tests, dict) or any(
        tests.get(name) != "PASS"
        for name in ("binary16", "dimension_hash", "front_face_probe")
    ):
        raise StableFailure("harness_self_test", "self-test coverage failed")
    front_face_probe = result.get("front_face_probe")
    if (
        not isinstance(front_face_probe, dict)
        or front_face_probe.get("version") != "front-face-probe-v1"
        or not isinstance(front_face_probe.get("clockwise_state_front"), bool)
        or not isinstance(
            front_face_probe.get("counter_clockwise_state_front"), bool
        )
        or front_face_probe["clockwise_state_front"]
        == front_face_probe["counter_clockwise_state_front"]
    ):
        raise StableFailure("harness_self_test", "front-face probe invalid")
    _validate_runtime_components(result.get("runtime_components"))
    return result


def _hash_bytes(data: bytes) -> OrderedDict[str, Any]:
    return OrderedDict(
        (
            ("sha1", hashlib.sha1(data).hexdigest()),
            ("sha256", hashlib.sha256(data).hexdigest()),
            ("size", len(data)),
        )
    )


def _hash_file(path: str, include_sha1: bool = True) -> OrderedDict[str, Any]:
    data = Path(path).read_bytes()
    if include_sha1:
        return _hash_bytes(data)
    return OrderedDict(
        (("sha256", hashlib.sha256(data).hexdigest()), ("size", len(data)))
    )


def _sha256_artifact(data: bytes) -> OrderedDict[str, Any]:
    return OrderedDict(
        (("sha256", hashlib.sha256(data).hexdigest()), ("size", len(data)))
    )


def _capture_file(path: str, label: str) -> dict[str, Any]:
    data = Path(path).read_bytes()
    return {
        "label": label,
        "path": os.path.abspath(path),
        "data": data,
        "artifact": _sha256_artifact(data),
    }


def _verify_captured_files(captures: Iterable[dict[str, Any]]) -> list[str]:
    changed: list[str] = []
    for capture in captures:
        try:
            current = Path(capture["path"]).read_bytes()
        except OSError:
            current = b""
        if current != capture["data"]:
            changed.append(str(capture["label"]))
    return sorted(set(changed))


def _capture_hlsl_closure(
    source: str,
) -> tuple[OrderedDict[str, Any], list[dict[str, Any]]]:
    pending = [os.path.abspath(source)]
    visited: set[str] = set()
    captures: list[dict[str, Any]] = []
    include_pattern = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
    repository = os.path.abspath(REPO_ROOT)
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        try:
            if os.path.commonpath((repository, path)) != repository:
                raise StableFailure("feature_source_closure", "include escaped repository")
        except ValueError as error:
            raise StableFailure(
                "feature_source_closure", "include escaped repository"
            ) from error
        data = Path(path).read_bytes()
        label = os.path.relpath(path, REPO_ROOT).replace("\\", "/")
        captures.append(
            {
                "label": label,
                "path": path,
                "data": data,
                "artifact": _sha256_artifact(data),
            }
        )
        text = data.decode("utf-8-sig")
        for include in include_pattern.findall(text):
            included = os.path.abspath(os.path.join(os.path.dirname(path), include))
            if not os.path.isfile(included):
                raise StableFailure("feature_source_closure", include)
            pending.append(included)
    captures.sort(key=lambda item: item["label"])
    combined = hashlib.sha256()
    records: list[OrderedDict[str, Any]] = []
    for capture in captures:
        label = capture["label"]
        data = capture["data"]
        label_bytes = label.encode("utf-8")
        combined.update(len(label_bytes).to_bytes(8, "little"))
        combined.update(label_bytes)
        combined.update(len(data).to_bytes(8, "little"))
        combined.update(data)
        records.append(
            OrderedDict(
                (
                    ("label", label),
                    ("sha256", capture["artifact"]["sha256"]),
                    ("size", capture["artifact"]["size"]),
                )
            )
        )
    report = OrderedDict(
        (("algorithm", "sha256"), ("combined_sha256", combined.hexdigest()), ("files", records))
    )
    return report, captures


def _hash_hlsl_closure(source: str) -> OrderedDict[str, Any]:
    report, _ = _capture_hlsl_closure(source)
    return report


def _write_hlsl_snapshot(
    root: str,
    captures: Iterable[dict[str, Any]],
    source_label: str,
) -> str:
    for capture in captures:
        destination = Path(root, *str(capture["label"]).split("/"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(capture["data"])
    source = Path(root, *source_label.split("/"))
    if not source.is_file():
        raise StableFailure("feature_source_snapshot", source_label)
    return str(source)


def _canonical_bytes(value: Any) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=True,
            allow_nan=False,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def _write_canonical(path: str, value: Any) -> None:
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(_canonical_bytes(value))


def _has_absolute_path(value: Any) -> bool:
    if isinstance(value, dict):
        return any(_has_absolute_path(item) for item in value.values())
    if isinstance(value, list):
        return any(_has_absolute_path(item) for item in value)
    if not isinstance(value, str):
        return False
    compiler_switches = {
        "/nologo",
        "/T",
        "/O3",
        "/E",
        "/I",
        "/D",
        "/Fo",
    }
    return bool(
        re.search(r"(^|[\s\"'])[A-Za-z]:[\\/]", value)
        or value.startswith("\\\\")
        or (value.startswith("/") and value not in compiler_switches)
    )


def _assert_receipt_safe(value: Any) -> None:
    if _has_absolute_path(value):
        raise StableFailure("unsafe_report", "report contains an absolute path", 3)


def _load_json(path: str, failure_code: str) -> Any:
    try:
        with open(path, "r", encoding="utf-8-sig") as stream:
            return json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        raise StableFailure(failure_code, error.__class__.__name__) from error


def validate_contracts(contracts: Any) -> None:
    if not isinstance(contracts, dict):
        raise StableFailure("contracts_schema", "root must be an object")
    if contracts.get("schema") != CONTRACTS_SCHEMA or contracts.get("schema_version") != 1:
        raise StableFailure("contracts_schema", "unsupported schema")
    if contracts.get("harness_version") != HARNESS_VERSION:
        raise StableFailure("contracts_schema", "unsupported harness version")
    fixtures = contracts.get("fixtures")
    if fixtures != list(FIXTURE_ORDER):
        raise StableFailure("contracts_schema", "fixtures must be adversarial,native")
    minimum = contracts.get("minimum_bucket_population")
    if not isinstance(minimum, int) or minimum <= 0:
        raise StableFailure("contracts_schema", "invalid minimum bucket population")
    profiles = contracts.get("profiles")
    if not isinstance(profiles, dict) or not profiles:
        raise StableFailure("contracts_schema", "profiles missing")
    for profile_name, profile in profiles.items():
        predicates = profile.get("predicates")
        required = profile.get("required_buckets")
        if not isinstance(predicates, dict) or not isinstance(required, dict):
            raise StableFailure("contracts_schema", f"{profile_name}: bucket schema")
        for fixture in FIXTURE_ORDER:
            names = required.get(fixture)
            if not isinstance(names, list) or len(names) != len(set(names)):
                raise StableFailure(
                    "contracts_schema",
                    f"{profile_name}/{fixture}: buckets must be unique",
                )
            missing = [name for name in names if not predicates.get(name)]
            if missing:
                raise StableFailure(
                    "contracts_schema",
                    f"{profile_name}/{fixture}: predicate missing for {missing[0]}",
                )
    contract_entries = contracts.get("contracts")
    if not isinstance(contract_entries, list) or not contract_entries:
        raise StableFailure("contracts_schema", "contracts missing")
    names = [entry.get("name") for entry in contract_entries]
    if len(names) != len(set(names)):
        raise StableFailure("contracts_schema", "contract names are not unique")
    for entry in contract_entries:
        if entry.get("profile") not in profiles:
            raise StableFailure("contracts_schema", "contract profile is unknown")
        if not isinstance(entry.get("ibl_in_light"), bool):
            raise StableFailure("contracts_schema", "ibl_in_light must be boolean")
    mutations = contracts.get("mutations")
    if not isinstance(mutations, list):
        raise StableFailure("contracts_schema", "mutations are required")
    mutation_ids = [entry.get("id") for entry in mutations]
    if len(mutation_ids) != len(set(mutation_ids)):
        raise StableFailure("contracts_schema", "mutations must be unique")
    if set(mutation_ids) != REQUIRED_MUTATION_IDS:
        raise StableFailure("contracts_schema", "required mutation set mismatch")
    known = set(names)
    for mutation in mutations:
        if not mutation.get("class"):
            raise StableFailure("contracts_schema", "mutation class missing")
        if mutation.get("target") not in known:
            raise StableFailure("contracts_schema", "mutation target is unknown")
        if mutation.get("fixture") not in FIXTURE_ORDER:
            raise StableFailure("contracts_schema", "mutation fixture is unknown")
        target = next(item for item in contract_entries if item["name"] == mutation["target"])
        required = profiles[target["profile"]]["required_buckets"][mutation["fixture"]]
        if mutation.get("expected_bucket") not in required:
            raise StableFailure("contracts_schema", "mutation bucket is not required")
def _mutation_replacements_digest(value: Any) -> str | None:
    if (
        not isinstance(value, list)
        or not value
        or any(
            not isinstance(item, dict)
            or set(item) != {"old", "new"}
            or not isinstance(item["old"], str)
            or not item["old"]
            or not isinstance(item["new"], str)
            for item in value
        )
    ):
        return None
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def validate_additive_features(features: Any) -> None:
    if not isinstance(features, dict) or set(features) != {"wetness-effects"}:
        raise StableFailure("feature_contracts_schema", "wetness-effects missing")
    feature = features["wetness-effects"]
    if (
        not isinstance(feature, dict)
        or feature.get("evidence_class") != "additive-feature"
        or feature.get("comparison")
        != "reconstructed-stock-vs-reconstructed-feature"
        or feature.get("native_bytecode_used") is not False
        or feature.get("profile") != "wetness-directional-lighting"
        or feature.get("measurement_protocol") != "wetness-warp-v5"
        or feature.get("source")
        != "shaders/lighting/bsdf_light_deferred.hlsl"
    ):
        raise StableFailure("feature_contracts_schema", "identity")
    minimum = feature.get("minimum_bucket_population")
    dimensions = feature.get("dimensions")
    if (
        not isinstance(minimum, int)
        or minimum <= 0
        or not isinstance(dimensions, dict)
        or not isinstance(dimensions.get("minimum_width"), int)
        or not isinstance(dimensions.get("minimum_height"), int)
        or dimensions["minimum_width"] < 1
        or dimensions["minimum_height"] < 1
    ):
        raise StableFailure("feature_contracts_schema", "dimensions")
    if feature.get("resource_delta") != {
        "kind": "Texture2D",
        "bind_point": 4,
        "value_type": "float",
    }:
        raise StableFailure("feature_contracts_schema", "resource delta")
    variants = feature.get("variants")
    if not isinstance(variants, list) or variants != [
        {
            "id": "directional",
            "defines": ["LIGHT_TYPE=1"],
            "ibl": "inactive",
        },
        {
            "id": "directional-ibl",
            "defines": ["AMBIENT_IBL_IN_LIGHT=1", "LIGHT_TYPE=1"],
            "ibl": "active",
        },
    ]:
        raise StableFailure("feature_contracts_schema", "variants")
    ambient = feature.get("ambient_runtime")
    if (
        not isinstance(ambient, dict)
        or ambient.get("id") != "ambient-runtime"
        or ambient.get("profile") != "wetness-ambient-ibl-runtime"
        or ambient.get("source")
        != "shaders/lighting/ambient_ibl_pass_runtime.hlsl"
        or ambient.get("resource_delta")
        != {
            "kind": "Texture2D",
            "bind_point": 13,
            "value_type": "float",
        }
        or ambient.get("variants")
        != [{"id": "ambient-runtime", "defines": []}]
        or ambient.get("scenario_buckets")
        != [
            "combined",
            "diffuse",
            "reflection",
            "matte",
            "no-ibl",
            "layered-matte-grazing",
        ]
        or ambient.get("output_buckets") != ["color"]
    ):
        raise StableFailure("feature_contracts_schema", "ambient runtime")
    ambient_mutation = ambient.get("mutation")
    if (
        not isinstance(ambient_mutation, dict)
        or ambient_mutation.get("id")
        != "ambient-wetness-old-output-multiply"
        or ambient_mutation.get("class") != "historical-regression"
        or ambient_mutation.get("expected_failed_property")
        != "matte_sheen"
        or _mutation_replacements_digest(
            ambient_mutation.get("replacements")
        )
        != "3b435c4cb902dae8ed56172275b562ceee7f44f2dad4d815ca67d8449c5d8619"
        or "old" in ambient_mutation
        or "new" in ambient_mutation
    ):
        raise StableFailure(
            "feature_contracts_schema", "ambient mutation"
        )
    fixtures = feature.get("fixtures")
    if fixtures != [
        {"id": "adversarial", "wetness_format": "R32_FLOAT"},
        {"id": "native", "wetness_format": "R8_UNORM"},
    ]:
        raise StableFailure("feature_contracts_schema", "fixtures")
    expected_lists = {
        "mask_buckets": ["mask.zero", "mask.partial", "mask.full"],
        "material_buckets": ["material.default", "material.skin"],
        "mrt_buckets": ["diffuse", "specular"],
        "monotonic_levels": [0.0, 0.25, 0.5, 0.75, 1.0],
    }
    for name, expected in expected_lists.items():
        if feature.get(name) != expected:
            raise StableFailure("feature_contracts_schema", name)
    mutations = feature.get("mutations")
    expected_mutations = [
        (
            "directional-wetness-old-substrate-floors-output-multiply",
            "historical-regression",
            "directional_layering",
            ["directional", "directional-ibl"],
            "7f9ff7c84aaf44d072d5eb6f9933a76a0b3de38eb09e81e82cf75b849a9303c6",
        ),
        (
            "directional-wetness-historical-pi-065-derating",
            "historical-regression",
            "wet_lobe_scale",
            ["directional"],
            "40223248e41858c1bc44a78a8b41586314b44ba7b3def2a1738bf434c69ca9ae",
        ),
        (
            "directional-wetness-ambient-ibl-untouched",
            "omission-regression",
            "ambient_ibl_layering",
            ["directional-ibl"],
            "060d5d39e0ba948a12af6aea06c1db13484e90432578feedc795f624c30966d8",
        ),
    ]
    if not isinstance(mutations, list) or len(mutations) != 3:
        raise StableFailure("feature_contracts_schema", "mutations")
    for mutation, expected in zip(mutations, expected_mutations):
        if (
            not isinstance(mutation, dict)
            or mutation.get("id") != expected[0]
            or mutation.get("class") != expected[1]
            or mutation.get("expected_failed_property") != expected[2]
            or mutation.get("variants") != expected[3]
            or _mutation_replacements_digest(mutation.get("replacements"))
            != expected[4]
            or "old" in mutation
            or "new" in mutation
        ):
            raise StableFailure("feature_contracts_schema", "mutation")


def _feature_target_contract(
    feature_contract: dict[str, Any], target: str
) -> dict[str, Any]:
    if target == "directional":
        contract = dict(feature_contract)
        contract["mutation"] = feature_contract["mutations"][0]
    elif target == "ambient-runtime":
        contract = dict(feature_contract)
        contract.update(feature_contract["ambient_runtime"])
        contract.pop("mutations", None)
    else:
        raise StableFailure("feature_contracts_schema", "unknown target")
    contract["target"] = target
    return contract


def _feature_mutations(
    target_contract: dict[str, Any],
) -> list[dict[str, Any]]:
    mutations = target_contract.get("mutations")
    if isinstance(mutations, list):
        return mutations
    return [target_contract["mutation"]]


def _replace_exact(text: str, old: str, new: str, transform: str) -> str:
    count = text.count(old)
    if count != 1:
        raise StableFailure(
            "mutation_transform",
            f"{transform}: expected one source match, found {count}",
        )
    return text.replace(old, new, 1)


AMBIENT_DEPTH_OLD = "bool isNearPath = (depth < 0.01);"
AMBIENT_DEPTH_NEW = "bool isNearPath = (depth <= 0.01);"
RUNTIME_AMBIENT_DEPTH_OLD = "bool isNearPath = depth < 0.01;"
RUNTIME_AMBIENT_DEPTH_NEW = "bool isNearPath = depth <= 0.01;"
DIRECTIONAL_DEPTH_OLD = """// Insn 4-17: depth-based matrix select.
    // Per-row ternary matches corpus shape closer than `float4x4` ?:.
    bool isNearPath = (depth < 0.01);"""
DIRECTIONAL_DEPTH_NEW = """// Insn 4-17: depth-based matrix select.
    // Per-row ternary matches corpus shape closer than `float4x4` ?:.
    bool isNearPath = (depth <= 0.01);"""
POINT_DEPTH_OLD = """// Insn 6-19: depth-based matrix select (same pattern as directional).
    bool isNearPath = (depth < 0.01);"""
POINT_DEPTH_NEW = """// Insn 6-19: depth-based matrix select (same pattern as directional).
    // Native near select is inclusive (exec-diff verified at exactly 0.01).
    bool isNearPath = (depth <= 0.01);"""
VLS_DEPTH_OLD = """// Per-row ternary matches corpus shape closer than `float4x4` ?:.
    bool isNearPath = (depth < 0.01);"""
VLS_DEPTH_NEW = """// Per-row ternary matches corpus shape closer than `float4x4` ?:.
    // Native near select is inclusive (exec-diff verified at exactly 0.01).
    bool isNearPath = (depth <= 0.01);"""


def _ensure_inclusive(text: str, old: str, new: str, transform: str) -> str:
    # Idempotent: the shipped source may already carry the inclusive form.
    if old not in text and text.count(new) == 1:
        return text
    return _replace_exact(text, old, new, transform)


def apply_inclusive_depth_control(source: str, target: str) -> str:
    if target == "ambient_ibl_pass":
        return _ensure_inclusive(
            source, AMBIENT_DEPTH_OLD, AMBIENT_DEPTH_NEW, "inclusive-depth-control"
        )
    if target == "ambient_ibl_pass_runtime":
        return _ensure_inclusive(
            source,
            RUNTIME_AMBIENT_DEPTH_OLD,
            RUNTIME_AMBIENT_DEPTH_NEW,
            "inclusive-depth-control",
        )
    if target.startswith("bsdf_light_deferred_directional"):
        return _ensure_inclusive(
            source,
            DIRECTIONAL_DEPTH_OLD,
            DIRECTIONAL_DEPTH_NEW,
            "inclusive-depth-control",
        )
    if target == "bsdf_light_deferred_point":
        return _ensure_inclusive(
            source,
            POINT_DEPTH_OLD,
            POINT_DEPTH_NEW,
            "inclusive-depth-control",
        )
    if target == "vls_slice_scatter":
        return _ensure_inclusive(
            source,
            VLS_DEPTH_OLD,
            VLS_DEPTH_NEW,
            "inclusive-depth-control",
        )
    return source


def apply_mutation(source: str, mutation_id: str) -> str:
    if mutation_id == "depth-exclusive-boundary":
        return _replace_exact(
            source,
            AMBIENT_DEPTH_NEW,
            AMBIENT_DEPTH_OLD,
            mutation_id,
        )
    if mutation_id == "point-depth-exclusive-boundary":
        return _replace_exact(
            source,
            POINT_DEPTH_NEW,
            POINT_DEPTH_OLD,
            mutation_id,
        )
    if mutation_id == "runtime-depth-exclusive-boundary":
        return _replace_exact(
            source,
            RUNTIME_AMBIENT_DEPTH_NEW,
            RUNTIME_AMBIENT_DEPTH_OLD,
            mutation_id,
        )
    if mutation_id == "vls-depth-exclusive-boundary":
        return _replace_exact(
            source,
            VLS_DEPTH_NEW,
            VLS_DEPTH_OLD,
            mutation_id,
        )
    if mutation_id == "transposed-reprojection":
        old = """posViewH.x = dot(reprojRow0, pos4);
        posViewH.y = dot(reprojRow1, pos4);
        posViewH.z = dot(reprojRow2, pos4);
        posViewH.w = dot(reprojRow3, pos4);"""
        new = """posViewH.x = dot(float4(reprojRow0.x, reprojRow1.x, reprojRow2.x, reprojRow3.x), pos4);
        posViewH.y = dot(float4(reprojRow0.y, reprojRow1.y, reprojRow2.y, reprojRow3.y), pos4);
        posViewH.z = dot(float4(reprojRow0.z, reprojRow1.z, reprojRow2.z, reprojRow3.z), pos4);
        posViewH.w = dot(float4(reprojRow0.w, reprojRow1.w, reprojRow2.w, reprojRow3.w), pos4);"""
        return _replace_exact(source, old, new, mutation_id)
    if mutation_id == "missing-axis-zyx":
        old = """float3 reflWorld;
        reflWorld.x = dot(ViewToWorld_row0.xyz, reflView);
        reflWorld.y = dot(ViewToWorld_row1.xyz, reflView);
        reflWorld.z = dot(ViewToWorld_row2.xyz, reflView);"""
        new = """float3 axis = reflView.zyx;
        float3 reflWorld;
        reflWorld.x = dot(ViewToWorld_row0.xyz, axis);
        reflWorld.y = dot(ViewToWorld_row1.xyz, axis);
        reflWorld.z = dot(ViewToWorld_row2.xyz, axis);"""
        return _replace_exact(source, old, new, mutation_id)
    if mutation_id == "flipped-view-direction":
        return _replace_exact(
            source,
            "float3 viewDirNeg  = -posView * posViewLen;",
            "float3 viewDirNeg  = posView * posViewLen;",
            mutation_id,
        )
    if mutation_id == "omitted-t12-sample":
        return _replace_exact(
            source,
            "float3 probeB = g_tAmbientProbeB.SampleLevel(g_sAmbientProbeB, uv, 0).xyz;",
            "float3 probeB = g_tAmbientProbeB.SampleLevel(g_sAmbientProbeB, uv, 0).xyz * cb0_idx0_screen_scale_and_blur_tolerance.w;",
            mutation_id,
        )
    if mutation_id == "wrong-texture-slot":
        return _replace_exact(
            source,
            "float3 probeB = g_tAmbientProbeB.SampleLevel(g_sAmbientProbeB, uv, 0).xyz;",
            "float3 probeB = g_tAmbientDiffuseB.SampleLevel(g_sAmbientDiffuseB, uv, 0).xyz + g_tAmbientProbeB.SampleLevel(g_sAmbientProbeB, uv, 0).xyz * cb0_idx0_screen_scale_and_blur_tolerance.w;",
            mutation_id,
        )
    if mutation_id == "omitted-material-branch":
        old = "bool isSkin = (abs(shadingData.z * 255.0 - 5.0) < 0.25);"
        new = """bool isSkin = (abs(shadingData.z * 255.0 - 5.0) < 0.25)
                   && (cb0_idx0_screen_scale_and_blur_tolerance.w != 0.0);"""
        return _replace_exact(source, old, new, mutation_id)
    raise StableFailure("mutation_transform", f"unknown mutation {mutation_id}")


def build_mutation_sources(
    original: str, target: str, mutation_id: str
) -> tuple[str, str]:
    control = apply_inclusive_depth_control(
        original.replace("\r\n", "\n"), target
    )
    if mutation_id == "missing-axis-zyx":
        old = """float3 reflWorld;
        reflWorld.x = dot(ViewToWorld_row0.xyz, reflView);
        reflWorld.y = dot(ViewToWorld_row1.xyz, reflView);
        reflWorld.z = dot(ViewToWorld_row2.xyz, reflView);"""
        control_block = """float3 axis = reflView.zyx;
        float3 reflViewForWorld = axis.zyx;
        float3 reflWorld;
        reflWorld.x = dot(ViewToWorld_row0.xyz, reflViewForWorld);
        reflWorld.y = dot(ViewToWorld_row1.xyz, reflViewForWorld);
        reflWorld.z = dot(ViewToWorld_row2.xyz, reflViewForWorld);"""
        control = _replace_exact(control, old, control_block, "missing-axis-control")
        mutant = _replace_exact(
            control,
            "float3 reflViewForWorld = axis.zyx;",
            "float3 reflViewForWorld = axis;",
            mutation_id,
        )
        return control, mutant
    return control, apply_mutation(control, mutation_id)


def build_additive_feature_mutant(
    original: str,
    feature_contract: dict[str, Any],
    mutation: dict[str, Any] | None = None,
) -> str:
    mutation = mutation or feature_contract["mutation"]
    normalized = original.replace("\r\n", "\n")
    replacements = mutation.get("replacements")
    if replacements is None:
        replacements = [{"old": mutation["old"], "new": mutation["new"]}]
    mutant = normalized
    for index, replacement in enumerate(replacements):
        mutant = _replace_exact(
            mutant,
            replacement["old"],
            replacement["new"],
            f"{mutation['id']}:{index}",
        )
    return mutant


def _compile_shader(
    fxc: str,
    source: str,
    include_directory: str,
    defines: Iterable[str],
    output: str,
) -> tuple[bool, str]:
    command = [fxc, *COMPILE_FLAGS, "/I", include_directory]
    for define in defines:
        command += ["/D", define]
    command += ["/Fo", output, source]
    process = subprocess.run(command, capture_output=True, text=True)
    return process.returncode == 0, (process.stdout or "") + (process.stderr or "")


def _windows_file_version(path: str) -> str:
    if os.name != "nt":
        return "unknown"
    try:
        size = ctypes.windll.version.GetFileVersionInfoSizeW(path, None)
        if not size:
            return "unknown"
        buffer = ctypes.create_string_buffer(size)
        if not ctypes.windll.version.GetFileVersionInfoW(path, 0, size, buffer):
            return "unknown"
        value = ctypes.c_void_p()
        length = ctypes.c_uint()
        if not ctypes.windll.version.VerQueryValueW(
            buffer, "\\", ctypes.byref(value), ctypes.byref(length)
        ):
            return "unknown"
        class FixedFileInfo(ctypes.Structure):
            _fields_ = [
                ("signature", ctypes.c_uint32),
                ("struct_version", ctypes.c_uint32),
                ("file_version_ms", ctypes.c_uint32),
                ("file_version_ls", ctypes.c_uint32),
                ("product_version_ms", ctypes.c_uint32),
                ("product_version_ls", ctypes.c_uint32),
                ("file_flags_mask", ctypes.c_uint32),
                ("file_flags", ctypes.c_uint32),
                ("file_os", ctypes.c_uint32),
                ("file_type", ctypes.c_uint32),
                ("file_subtype", ctypes.c_uint32),
                ("file_date_ms", ctypes.c_uint32),
                ("file_date_ls", ctypes.c_uint32),
            ]

        info = ctypes.cast(value, ctypes.POINTER(FixedFileInfo)).contents
        return (
            f"{info.file_version_ms >> 16}.{info.file_version_ms & 0xFFFF}."
            f"{info.file_version_ls >> 16}.{info.file_version_ls & 0xFFFF}"
        )
    except (AttributeError, OSError, ValueError):
        return "unknown"


def _validate_runtime_components(value: Any) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise StableFailure("runtime_identity", "runtime components missing")
    expected_names = ["d3d10warp.dll", "d3d11.dll"]
    names = [item.get("name") for item in value if isinstance(item, dict)]
    if names != expected_names or len(names) != len(value):
        raise StableFailure("runtime_identity", "runtime component order")
    for component in value:
        if component.get("state") != "available":
            raise StableFailure(
                "runtime_identity", f"{component.get('name', 'unknown')}:unavailable"
            )
        if "path" in component or _has_absolute_path(component):
            raise StableFailure("runtime_identity", "runtime component path")
        if (
            not isinstance(component.get("version"), str)
            or re.fullmatch(r"\d+\.\d+\.\d+\.\d+", component["version"]) is None
            or not isinstance(component.get("sha256"), str)
            or re.fullmatch(r"[0-9a-f]{64}", component["sha256"]) is None
            or not isinstance(component.get("size"), int)
            or component["size"] <= 0
        ):
            raise StableFailure("runtime_identity", "runtime component identity")
    return value


def _compiler_banner(fxc: str) -> str:
    process = subprocess.run([fxc, "/?"], capture_output=True, text=True)
    text = (process.stdout or "") + (process.stderr or "")
    for line in text.splitlines():
        stripped = line.strip()
        if stripped and not _has_absolute_path(stripped):
            return stripped[:160]
    return "Microsoft Direct3D Shader Compiler"


def _measurement_metrics_zero() -> OrderedDict[str, Any]:
    return OrderedDict(
        (
            ("total_pixels", 0),
            ("total_channels", 0),
            ("divergent_channels", 0),
            ("divergent_pixels", 0),
            ("max_absolute_error", 0.0),
            ("mean_absolute_error", 0.0),
            ("max_relative_error", 0.0),
            ("mean_relative_error", 0.0),
        )
    )


def _merge_metrics(metrics: Iterable[dict[str, Any]]) -> OrderedDict[str, Any]:
    result = _measurement_metrics_zero()
    absolute_sum = 0.0
    relative_sum = 0.0
    for item in metrics:
        channels = int(item.get("total_channels", 0))
        result["total_pixels"] += int(item.get("total_pixels", 0))
        result["total_channels"] += channels
        result["divergent_channels"] += int(item.get("divergent_channels", 0))
        result["divergent_pixels"] += int(item.get("divergent_pixels", 0))
        result["max_absolute_error"] = max(
            result["max_absolute_error"], float(item.get("max_absolute_error", 0.0))
        )
        result["max_relative_error"] = max(
            result["max_relative_error"], float(item.get("max_relative_error", 0.0))
        )
        absolute_sum += float(item.get("mean_absolute_error", 0.0)) * channels
        relative_sum += float(item.get("mean_relative_error", 0.0)) * channels
    if result["total_channels"]:
        result["mean_absolute_error"] = absolute_sum / result["total_channels"]
        result["mean_relative_error"] = relative_sum / result["total_channels"]
    return result


def validate_measurement(
    measurement: Any,
    profile: str,
    fixture: str,
    required_buckets: list[str],
    minimum_population: int,
    width: int,
    height: int,
    expected_source_sha256: str,
) -> list[OrderedDict[str, Any]]:
    failures: list[OrderedDict[str, Any]] = []
    if (
        not isinstance(measurement, dict)
        or measurement.get("schema") != MEASUREMENT_SCHEMA
        or measurement.get("schema_version") != 1
    ):
        return [OrderedDict((("code", "measurement_protocol"), ("detail", "schema")))]
    if measurement.get("harness_version") != HARNESS_VERSION:
        failures.append(
            OrderedDict(
                (("code", "measurement_protocol"), ("detail", "harness_version"))
            )
        )
    if measurement.get("source_sha256") != expected_source_sha256:
        failures.append(
            OrderedDict((("code", "stale_harness"), ("detail", "source_sha256")))
        )
    if measurement.get("profile") != profile:
        failures.append(
            OrderedDict((("code", "profile_mismatch"), ("detail", profile)))
        )
    if measurement.get("fixture") != fixture:
        failures.append(
            OrderedDict((("code", "fixture_mismatch"), ("detail", fixture)))
        )
    if measurement.get("width") != width or measurement.get("height") != height:
        failures.append(
            OrderedDict((("code", "measurement_protocol"), ("detail", "dimensions")))
        )
    execution_environment = measurement.get("execution_environment")
    if (
        not isinstance(execution_environment, dict)
        or execution_environment.get("driver_type") != "WARP"
        or execution_environment.get("feature_level") != "11_0"
        or not execution_environment.get("runtime_fingerprint")
        or not execution_environment.get("limitation")
    ):
        failures.append(
            OrderedDict(
                (("code", "measurement_protocol"), ("detail", "execution_environment"))
            )
        )
    harness_failures = measurement.get("failures")
    if not isinstance(harness_failures, list):
        failures.append(
            OrderedDict((("code", "measurement_protocol"), ("detail", "failures")))
        )
        harness_failures = []
    if harness_failures:
        return failures

    digest = measurement.get("generated_inputs_sha256")
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        failures.append(
            OrderedDict((("code", "measurement_protocol"), ("detail", "input_hash")))
        )
    if not isinstance(measurement.get("seed_base"), int):
        failures.append(
            OrderedDict((("code", "measurement_protocol"), ("detail", "seed_base")))
        )
    for field in ("seeds", "scenario_seeds", "formats"):
        if not isinstance(measurement.get(field), list):
            failures.append(
                OrderedDict((("code", "measurement_protocol"), ("detail", field)))
            )
    if measurement.get("measurement_format") != "R32G32B32A32_FLOAT":
        failures.append(
            OrderedDict(
                (("code", "measurement_protocol"), ("detail", "measurement_format"))
            )
        )
    matrix = measurement.get("matrix_assertions")
    if not isinstance(matrix, dict) or matrix.get("verdict") != "PASS":
        failures.append(
            OrderedDict(
                (("code", "measurement_protocol"), ("detail", "matrix_assertions"))
            )
        )

    def validate_metrics(value: Any, detail: str) -> bool:
        fields = (
            "total_pixels",
            "total_channels",
            "divergent_channels",
            "divergent_pixels",
            "max_absolute_error",
            "mean_absolute_error",
            "max_relative_error",
            "mean_relative_error",
        )
        if not isinstance(value, dict) or any(field not in value for field in fields):
            failures.append(
                OrderedDict((("code", "measurement_protocol"), ("detail", detail)))
            )
            return False
        if any(
            not isinstance(value[field], (int, float))
            or isinstance(value[field], bool)
            or not math.isfinite(float(value[field]))
            or value[field] < 0
            for field in fields
        ):
            failures.append(
                OrderedDict((("code", "measurement_protocol"), ("detail", detail)))
            )
            return False
        if (
            value["divergent_pixels"] > value["total_pixels"]
            or value["divergent_channels"] > value["total_channels"]
        ):
            failures.append(
                OrderedDict((("code", "measurement_protocol"), ("detail", detail)))
            )
            return False
        return True

    aggregate_valid = validate_metrics(measurement.get("aggregate"), "aggregate")
    if aggregate_valid and (
        measurement["aggregate"]["total_pixels"] == 0
        or measurement["aggregate"]["total_channels"] == 0
    ):
        failures.append(
            OrderedDict((("code", "measurement_protocol"), ("detail", "aggregate_empty")))
        )
        aggregate_valid = False
    buckets = measurement.get("buckets")
    if not isinstance(buckets, list):
        failures.append(
            OrderedDict((("code", "measurement_protocol"), ("detail", "buckets")))
        )
        return failures
    names = [item.get("name") for item in buckets if isinstance(item, dict)]
    if len(names) != len(buckets) or names != sorted(set(names)):
        failures.append(
            OrderedDict((("code", "measurement_protocol"), ("detail", "bucket_order")))
        )
    by_name = {item.get("name"): item for item in buckets if isinstance(item, dict)}
    for name, bucket in by_name.items():
        if (
            not isinstance(name, str)
            or not isinstance(bucket.get("population"), int)
            or bucket.get("population", -1) < 0
            or not isinstance(bucket.get("required_minimum"), int)
        ):
            failures.append(
                OrderedDict((("code", "measurement_protocol"), ("detail", "bucket")))
            )
            continue
        bucket_valid = validate_metrics(bucket, f"bucket:{name}")
        if bucket_valid and bucket["population"] > 0 and (
            bucket["total_pixels"] == 0 or bucket["total_channels"] == 0
        ):
            failures.append(
                OrderedDict(
                    (
                        ("code", "measurement_protocol"),
                        ("detail", f"bucket_empty:{name}"),
                    )
                )
            )
    for name in required_buckets:
        bucket = by_name.get(name)
        if bucket is None:
            failures.append(
                OrderedDict((("code", "missing_bucket"), ("detail", name)))
            )
        elif int(bucket.get("population", 0)) < minimum_population:
            failures.append(
                OrderedDict((("code", "bucket_population"), ("detail", name)))
            )
    if _has_absolute_path(measurement):
        failures.append(
            OrderedDict((("code", "measurement_protocol"), ("detail", "absolute_path")))
        )
    verdict = measurement.get("verdict")
    if aggregate_valid:
        expected_verdict = (
            "FAIL"
            if measurement["aggregate"]["divergent_pixels"] > 0
            else "PASS"
        )
        if verdict != expected_verdict:
            failures.append(
                OrderedDict((("code", "measurement_protocol"), ("detail", "verdict")))
            )
    return failures


def validate_harness_result(
    measurement: dict[str, Any],
    return_code: int,
    profile: str,
    fixture: str,
    required_buckets: list[str],
    minimum_population: int,
    detail: str,
    width: int,
    height: int,
    expected_source_sha256: str,
) -> list[OrderedDict[str, str]]:
    failures = validate_measurement(
        measurement,
        profile,
        fixture,
        required_buckets,
        minimum_population,
        width,
        height,
        expected_source_sha256,
    )
    raw_harness_failures = measurement.get("failures")
    harness_failures = (
        raw_harness_failures if isinstance(raw_harness_failures, list) else []
    )
    for harness_failure in harness_failures:
        if not isinstance(harness_failure, dict):
            failures.append(
                OrderedDict(
                    (("code", "measurement_protocol"), ("detail", detail))
                )
            )
            continue
        failures.append(
            OrderedDict(
                (
                    ("code", str(harness_failure.get("code", "harness_runtime"))),
                    ("detail", str(harness_failure.get("detail", detail))),
                )
            )
        )
    if return_code == 3:
        failures.append(
            OrderedDict((("code", "harness_runtime"), ("detail", detail)))
        )
    expected_codes = {"PASS": 0, "FAIL": 1, "UNPROVEN": 2}
    expected = expected_codes.get(measurement.get("verdict"))
    if return_code != 3 and (expected is None or return_code != expected):
        failures.append(
            OrderedDict(
                (("code", "measurement_protocol"), ("detail", f"{detail}:exit_code"))
            )
        )
    return failures


def _expected_wetness_mask_payloads(
    fixture: str,
    width: int,
    height: int,
    levels: Iterable[float],
) -> OrderedDict[str, bytes]:
    def float32(value: float) -> float:
        return struct.unpack("<f", struct.pack("<f", value))[0]

    def encode(values: Iterable[float]) -> bytes:
        if fixture == "native":
            encoded = bytearray()
            for value in values:
                product = float32(
                    float32(max(0.0, min(1.0, value))) * 255.0
                )
                encoded.append(int(math.floor(product + 0.5)))
            return bytes(encoded)
        return b"".join(struct.pack("<f", float32(value)) for value in values)

    pixel_count = width * height
    active = [0.0] * pixel_count
    ramp_begin = width // 4
    ramp_end = width // 2
    full_end = width * 3 // 4
    ramp_width = max(1, ramp_end - ramp_begin)
    native_minimum = float32(float32(1.0) / float32(255.0))
    native_maximum = float32(float32(254.0) / float32(255.0))
    for y in range(height):
        for x in range(ramp_begin, ramp_end):
            value = float32(
                float32(x - ramp_begin + 1)
                / float32(ramp_width + 1)
            )
            if fixture == "native":
                value = max(native_minimum, min(native_maximum, value))
            active[y * width + x] = value
        for x in range(ramp_end, full_end):
            active[y * width + x] = 1.0
    patch_size = max(2, min(width, height) // 8)
    patch_width = min(patch_size, width - 2 if width > 2 else 1)
    patch_height = min(patch_size, height - 2 if height > 2 else 1)
    patch_x = width - patch_width - 1 if width > patch_width + 1 else 0
    patch_y = (height - patch_height) // 2 if height > patch_height else 0
    for y in range(patch_y, patch_y + patch_height):
        for x in range(patch_x, patch_x + patch_width):
            active[y * width + x] = 1.0

    payloads: OrderedDict[str, bytes] = OrderedDict()
    payloads["active-pattern"] = encode(active)
    for index, level in enumerate(levels):
        payloads[f"level-{index}"] = encode(
            [float32(float(level))] * pixel_count
        )
    payloads["neutral-zero"] = encode([0.0] * pixel_count)
    return payloads


def _wetness_bucket_evidence_from_payload(
    fixture: str,
    payload: bytes,
) -> OrderedDict[str, OrderedDict[str, Any]]:
    if fixture == "native":
        values = [
            struct.unpack("<f", struct.pack("<f", value / 255.0))[0]
            for value in payload
        ]
    else:
        if len(payload) % 4 != 0:
            raise StableFailure(
                "feature_measurement_protocol", "active_mask_payload"
            )
        values = [
            value[0] for value in struct.iter_unpack("<f", payload)
        ]
    evidence: OrderedDict[str, OrderedDict[str, Any]] = OrderedDict()
    for name, predicate in (
        ("mask.zero", lambda value: value == 0.0),
        ("mask.partial", lambda value: 0.0 < value < 1.0),
        ("mask.full", lambda value: value == 1.0),
    ):
        selected = [value for value in values if predicate(value)]
        if not selected:
            raise StableFailure(
                "feature_measurement_protocol", f"active_mask:{name}"
            )
        evidence[name] = OrderedDict(
            (
                ("population", len(selected)),
                ("minimum", min(selected)),
                ("maximum", max(selected)),
            )
        )
    return evidence


def _feature_failure_record(
    code: str,
    detail: str,
) -> OrderedDict[str, Any]:
    return OrderedDict(
        (
            ("schema", FEATURE_MEASUREMENT_SCHEMA),
            ("schema_version", 1),
            ("evidence_class", "additive-feature"),
            (
                "comparison",
                "reconstructed-stock-vs-reconstructed-feature",
            ),
            ("native_bytecode_used", False),
            ("code", code),
            ("detail", detail),
        )
    )


def _validate_ambient_feature_measurement(
    measurement: Any,
    fixture: str,
    feature_contract: dict[str, Any],
    width: int,
    height: int,
    expected_source_sha256: str,
) -> list[OrderedDict[str, str]]:
    failures: list[OrderedDict[str, str]] = []
    target_contract = _feature_target_contract(
        feature_contract, "ambient-runtime"
    )

    def fail(detail: str) -> None:
        failures.append(
            _feature_failure_record(
                "feature_measurement_protocol", detail
            )
        )

    def number(value: Any) -> bool:
        return (
            isinstance(value, (int, float))
            and not isinstance(value, bool)
            and math.isfinite(float(value))
        )

    def close(left: Any, right: Any, tolerance: float = 1.0e-9) -> bool:
        return (
            number(left)
            and number(right)
            and math.isclose(
                float(left),
                float(right),
                rel_tol=tolerance,
                abs_tol=tolerance,
            )
        )

    if (
        not isinstance(measurement, dict)
        or measurement.get("schema") != FEATURE_MEASUREMENT_SCHEMA
        or measurement.get("schema_version") != 1
    ):
        fail("schema")
        return failures
    expected_scalars = {
        "harness_version": HARNESS_VERSION,
        "source_sha256": expected_source_sha256,
        "evidence_class": "additive-feature",
        "comparison": "reconstructed-stock-vs-reconstructed-feature",
        "native_bytecode_used": False,
        "target": "ambient-runtime",
        "profile": target_contract["profile"],
        "fixture": fixture,
        "width": width,
        "height": height,
        "measurement_protocol": feature_contract["measurement_protocol"],
        "wetness_format": next(
            item["wetness_format"]
            for item in feature_contract["fixtures"]
            if item["id"] == fixture
        ),
    }
    for name, expected in expected_scalars.items():
        if measurement.get(name) != expected:
            fail(name)
    raw_harness_failures = measurement.get("failures")
    if raw_harness_failures:
        if (
            not isinstance(raw_harness_failures, list)
            or any(
                not isinstance(item, dict)
                or item.get("schema") != FEATURE_MEASUREMENT_SCHEMA
                or item.get("schema_version") != 1
                or item.get("evidence_class") != "additive-feature"
                or item.get("comparison")
                != "reconstructed-stock-vs-reconstructed-feature"
                or item.get("native_bytecode_used") is not False
                or not isinstance(item.get("code"), str)
                or not isinstance(item.get("detail"), str)
                for item in raw_harness_failures
            )
        ):
            fail("failure_receipt")
        else:
            for item in raw_harness_failures:
                failures.append(
                    _feature_failure_record(item["code"], item["detail"])
                )
        if measurement.get("verdict") != "UNPROVEN":
            fail("failure_verdict")
        if _has_absolute_path(measurement):
            fail("absolute_path")
        return failures
    if raw_harness_failures is not None and raw_harness_failures != []:
        fail("failures")

    if measurement.get("measurement_format") != "R32G32B32A32_FLOAT":
        fail("measurement_format")
    formats = measurement.get("formats")
    if not isinstance(formats, list) or not formats:
        fail("formats")
    else:
        wetness_formats = [
            item
            for item in formats
            if isinstance(item, dict) and item.get("bind_point") == 13
        ]
        if (
            len(wetness_formats) != 1
            or wetness_formats[0].get("dimension") != "texture2d"
            or wetness_formats[0].get("resource_format")
            != expected_scalars["wetness_format"]
            or wetness_formats[0].get("srv_format")
            != expected_scalars["wetness_format"]
        ):
            fail("wetness_format_record")
    environment = measurement.get("execution_environment")
    if (
        not isinstance(environment, dict)
        or environment.get("driver_type") != "WARP"
        or environment.get("feature_level") != "11_0"
        or environment.get("native_bytecode_used") is not False
    ):
        fail("execution_environment")
    seeds = measurement.get("seeds")
    if (
        not isinstance(seeds, list)
        or len(seeds) != len(target_contract["scenario_buckets"])
        or any(not isinstance(seed, int) for seed in seeds)
    ):
        fail("seeds")
    generated = measurement.get("generated_inputs_sha256")
    if (
        not isinstance(generated, str)
        or re.fullmatch(r"[0-9a-f]{64}", generated) is None
    ):
        fail("generated_inputs_sha256")

    expected_mask_ids = [
        "active-pattern",
        "level-0",
        "level-1",
        "level-2",
        "level-3",
        "level-4",
        "neutral-zero",
    ]
    expected_mask_payloads = _expected_wetness_mask_payloads(
        fixture,
        width,
        height,
        feature_contract["monotonic_levels"],
    )
    expected_bucket_evidence = _wetness_bucket_evidence_from_payload(
        fixture, expected_mask_payloads["active-pattern"]
    )
    hashes = measurement.get("hashes")
    mask_records: dict[str, list[dict[str, Any]]] = {}
    if not isinstance(hashes, dict):
        fail("hashes")
    else:
        for name in ("uploaded_masks", "readback_masks"):
            records = hashes.get(name)
            if (
                not isinstance(records, list)
                or any(not isinstance(item, dict) for item in records)
            ):
                fail(name)
                continue
            if [item.get("id") for item in records] != expected_mask_ids:
                fail(f"{name}_order")
            for item in records:
                payload = expected_mask_payloads.get(item.get("id"))
                if (
                    payload is None
                    or item.get("sha256")
                    != hashlib.sha256(payload or b"").hexdigest()
                    or item.get("size") != len(payload or b"")
                ):
                    fail(f"{name}_record")
                    break
            mask_records[name] = records
        uploaded = mask_records.get("uploaded_masks")
        readback = mask_records.get("readback_masks")
        if uploaded is not None and readback is not None:
            if uploaded != readback:
                fail("mask_hash_pair")

    contract_delta = measurement.get("contract_delta")
    if (
        not isinstance(contract_delta, dict)
        or contract_delta.get("stock_to_feature")
        != "only-texture2d-t13-added"
        or contract_delta.get(
            "mutation_t13_optimization_away_allowed"
        )
        is not False
        or contract_delta.get("verdict") != "PASS"
    ):
        fail("contract_delta")
    if measurement.get("variants") != target_contract["variants"]:
        fail("variants")

    properties = measurement.get("properties")
    monotonicity: Any = None
    if not isinstance(properties, dict):
        fail("properties")
    else:
        neutral = properties.get("neutral_identity")
        if (
            not isinstance(neutral, dict)
            or neutral.get("tolerance_absolute") != 0
            or neutral.get("tolerance_relative") != 0
            or neutral.get("comparisons")
            != len(target_contract["scenario_buckets"])
            or neutral.get("violations") != []
            or neutral.get("verdict") != "PASS"
        ):
            fail("property:neutral_identity")
        locality = properties.get("active_locality")
        if (
            not isinstance(locality, dict)
            or locality.get("zero_tolerance") is not True
            or locality.get("bucket_basis")
            != (
                "t13 GPU readback after fixture quantization; "
                "scenario from controlled ambient inputs"
            )
            or locality.get("violations") != []
            or locality.get("verdict") != "PASS"
        ):
            fail("property:active_locality")
        elif locality.get("pattern") != {
            "exact_zero": True,
            "exact_one": True,
            "smooth_strict_partial_ramp": True,
            "isolated_full_patch_with_zero_moat": True,
        }:
            fail("active_locality:pattern")
        magnitude = properties.get("magnitude")
        if (
            not isinstance(magnitude, dict)
            or magnitude.get("rgb_only") is not True
            or magnitude.get("invalid_magnitude_buckets") != 0
            or magnitude.get("glossy_probe")
            != {
                "scenario": "reflection",
                "mask": "mask.full",
                "output": "color",
                "denominator_policy": "stock-energy-proven",
                "maximum_delta_fraction_of_stock": 2.5,
                "maximum_p95_absolute_delta": 0.9,
                "maximum_absolute_delta": 1.0,
                "maximum_positive_delta": 1.0,
                "inputs": {
                    "smoothness": 0.85,
                    "spec_magnitude": 0.35,
                    "decoded_material": 0.5,
                    "encoded_material": math.sqrt(0.5 * 0.02),
                    "maximum_encoded_material": math.sqrt(0.02),
                },
            }
            or magnitude.get("verdict") != "PASS"
        ):
            fail("property:magnitude")
        glossy_probe = (
            magnitude.get("glossy_probe")
            if isinstance(magnitude, dict)
            else None
        )
        glossy_inputs = (
            glossy_probe.get("inputs")
            if isinstance(glossy_probe, dict)
            else None
        )
        if (
            not isinstance(glossy_inputs, dict)
            or not number(glossy_inputs.get("encoded_material"))
            or not number(glossy_inputs.get("maximum_encoded_material"))
            or glossy_inputs["encoded_material"] < 0
            or glossy_inputs["encoded_material"]
            > glossy_inputs["maximum_encoded_material"]
            or glossy_inputs["maximum_encoded_material"]
            > math.sqrt(0.02) + 1.0e-12
        ):
            fail("magnitude:glossy_inputs")
        matte_sheen = properties.get("matte_sheen")
        if (
            not isinstance(matte_sheen, dict)
            or matte_sheen.get("probe")
            != {
                "scenario": "matte",
                "mask": "mask.full",
                "output": "color",
            }
            or matte_sheen.get("inputs")
            != {
                "smoothness": 0.25,
                "smoothness_maximum": 0.3,
                "spec_magnitude": 0.05,
                "encoded_material": math.sqrt(0.01 * 0.02),
            }
            or matte_sheen.get("film_model")
            != {
                "substrate_independent": True,
                "coverage_source": "t13",
                "roughness_formula": "max(saturate(1-wetness),0.05)",
                "minimum_water_roughness": 0.05,
                "smoothness_formula": "1-wetFilmRoughness",
                "strength_formula": "saturate(1-wetFilmRoughness)",
                "full_wetness_roughness": 0.05,
                "full_wetness_smoothness": 0.95,
                "f0": 0.02,
                "full_wetness_strength": 0.95,
                "fresnel_model": "schlick",
                "energy_attenuation": "substrate*(1-wetnessF)",
                "lobe_path": "ambient-ibl",
                "normal_incidence_reflection_scalar": 0.95 * 0.02,
            }
            or matte_sheen.get("signed_rule")
            != "feature-stock RGB strictly positive"
            or matte_sheen.get("stock_denominator") != "expected-zero"
            or matte_sheen.get("violations") != []
            or matte_sheen.get("verdict") != "PASS"
        ):
            fail("property:matte_sheen")
        no_ibl_film = properties.get("no_ibl_film")
        if (
            not isinstance(no_ibl_film, dict)
            or no_ibl_film.get("probe")
            != {
                "scenario": "no-ibl",
                "mask": "mask.full",
                "output": "color",
            }
            or no_ibl_film.get("inputs")
            != {
                "substrate_ibl_available": False,
                "material_probe_selector": 0,
                "smoothness": 0.25,
                "spec_magnitude": 0.05,
                "encoded_material": math.sqrt(0.01 * 0.02),
                "ambient_base": [0.2, 0.2, 0.2],
                "lit_scene": [0.6, 0.45, 0.3, 1],
                "lit_scene_weight": 0.75,
                "lit_scene_alpha": 1,
            }
            or no_ibl_film.get("signed_rule")
            != "feature-stock RGB strictly positive"
            or no_ibl_film.get("stock_denominator") != "proven"
            or no_ibl_film.get("violations") != []
            or no_ibl_film.get("verdict") != "PASS"
        ):
            fail("property:no_ibl_film")

        requested_levels = [
            float(value) for value in feature_contract["monotonic_levels"]
        ]
        uploaded_levels = [
            (
                math.floor(value * 255.0 + 0.5) / 255.0
                if fixture == "native"
                else value
            )
            for value in requested_levels
        ]
        film_blend = properties.get("ambient_film_blend")
        film_levels = (
            film_blend.get("levels")
            if isinstance(film_blend, dict)
            else None
        )
        film_substrate = (
            film_blend.get("substrate")
            if isinstance(film_blend, dict)
            else None
        )
        film_source = (
            film_blend.get("film_source")
            if isinstance(film_blend, dict)
            else None
        )
        if (
            not isinstance(film_blend, dict)
            or film_blend.get("probe")
            != {
                "scenario": "layered-matte-grazing",
                "mask": "levels",
                "output": "color",
            }
            or not number(film_blend.get("ndotv"))
            or not (0 < film_blend["ndotv"] <= 0.125)
            or not close(film_blend["ndotv"], 0.1, 1.0e-4)
            or film_blend.get("ndotv_range")
            != {"exclusive_minimum": 0, "inclusive_maximum": 0.125}
            or not isinstance(film_substrate, list)
            or len(film_substrate) != 3
            or any(not number(value) for value in film_substrate)
            or any(
                not close(value, expected, 1.0e-8)
                for value, expected in zip(
                    film_substrate, [0.5625, 0.375, 0.1875]
                )
            )
            or not isinstance(film_source, list)
            or len(film_source) != 3
            or any(not number(value) for value in film_source)
            or any(
                not close(value, expected, 1.0e-8)
                for value, expected in zip(
                    film_source, [0.1875, 0.375, 0.5625]
                )
            )
            or film_blend.get("formula")
            != "substrate*(1-wetnessF)+film*wetnessF"
            or film_blend.get("absolute_tolerance") != 2.0e-5
            or film_blend.get("relative_tolerance") != 2.0e-4
            or film_blend.get("maximum_energy_residual") != 6.0e-5
            or film_blend.get("violations") != []
            or film_blend.get("verdict") != "PASS"
            or not isinstance(film_levels, list)
            or len(film_levels) != len(requested_levels)
        ):
            fail("property:ambient_film_blend")
        elif isinstance(film_levels, list):
            for index, level in enumerate(film_levels):
                arrays = {
                    name: level.get(name)
                    for name in (
                        "expected_delta",
                        "observed_delta",
                        "expected_film",
                        "observed_film",
                    )
                } if isinstance(level, dict) else {}
                if (
                    not isinstance(level, dict)
                    or not close(level.get("requested"), requested_levels[index])
                    or not close(
                        level.get("uploaded"),
                        uploaded_levels[index],
                        1.0e-7,
                    )
                    or not number(level.get("wetness_f"))
                    or level["wetness_f"] < 0
                    or any(
                        not isinstance(values, list)
                        or len(values) != 3
                        or any(not number(value) for value in values)
                        for values in arrays.values()
                    )
                    or not number(level.get("maximum_residual"))
                    or level["maximum_residual"] < 0
                    or not number(level.get("expected_energy_delta"))
                    or not number(level.get("observed_energy_delta"))
                    or level.get("film_nonzero_required")
                    is not (index != 0)
                    or level.get("film_nonzero") is not True
                ):
                    fail("ambient_film_blend:level")
                    continue
                roughness = max(
                    min(
                        max(1.0 - float(level["uploaded"]), 0.0),
                        1.0,
                    ),
                    0.05,
                )
                strength = min(max(1.0 - roughness, 0.0), 1.0)
                one_minus_ndotv = 1.0 - float(film_blend["ndotv"])
                expected_wetness_f = strength * (
                    0.02 + 0.98 * one_minus_ndotv**5
                )
                if not close(
                    level["wetness_f"], expected_wetness_f, 1.0e-8
                ):
                    fail("ambient_film_blend:wetness_f")
                wetness_f = expected_wetness_f
                computed_maximum_residual = 0.0
                for channel in range(3):
                    expected_delta = wetness_f * (
                        float(film_source[channel]) -
                        float(film_substrate[channel])
                    )
                    expected_film = wetness_f * float(film_source[channel])
                    tolerance = 2.0e-5 + 2.0e-4 * max(
                        abs(float(film_substrate[channel])),
                        abs(
                            float(film_substrate[channel]) +
                            expected_delta
                        ),
                    )
                    if (
                        not close(
                            arrays["expected_delta"][channel],
                            expected_delta,
                            1.0e-8,
                        )
                        or not close(
                            arrays["expected_film"][channel],
                            expected_film,
                            1.0e-8,
                        )
                        or abs(
                            float(arrays["observed_delta"][channel]) -
                            expected_delta
                        )
                        > tolerance
                        or abs(
                            float(arrays["observed_film"][channel]) -
                            expected_film
                        )
                        > tolerance
                        or (
                            index != 0
                            and arrays["observed_film"][channel] <= 0
                        )
                    ):
                        fail("ambient_film_blend:formula")
                        break
                    computed_maximum_residual = max(
                        computed_maximum_residual,
                        abs(
                            float(arrays["observed_delta"][channel]) -
                            expected_delta
                        ),
                        abs(
                            float(arrays["observed_film"][channel]) -
                            expected_film
                        ),
                    )
                computed_expected_energy = sum(
                    float(value) for value in arrays["expected_delta"]
                )
                computed_observed_energy = sum(
                    float(value) for value in arrays["observed_delta"]
                )
                if (
                    not close(
                        level["maximum_residual"],
                        computed_maximum_residual,
                        1.0e-8,
                    )
                    or not close(
                        level["expected_energy_delta"],
                        computed_expected_energy,
                        1.0e-8,
                    )
                    or not close(
                        level["observed_energy_delta"],
                        computed_observed_energy,
                        1.0e-8,
                    )
                    or abs(computed_expected_energy) > 1.0e-8
                    or abs(computed_observed_energy) > 6.0e-5
                ):
                    fail("ambient_film_blend:energy")
        wet_lobe_scale = properties.get("wet_lobe_scale")
        if wet_lobe_scale is not None and (
            not isinstance(wet_lobe_scale, dict)
            or wet_lobe_scale.get("stock_scale_symbol")
            != "FO4_DIRECTIONAL_SPECULAR_SCALE"
            or wet_lobe_scale.get("film_scale_symbol")
            != "FO4_DIRECTIONAL_SPECULAR_SCALE"
            or wet_lobe_scale.get("stock_scale") != 3.141593
            or wet_lobe_scale.get("expected_derating_ratio") != 0.65
            or wet_lobe_scale.get("expected_difference_ratio") != 0.35
            or not isinstance(
                wet_lobe_scale.get("proven_channels"), int
            )
            or wet_lobe_scale["proven_channels"] <= 0
            or not number(wet_lobe_scale.get("corrected_lobe_mean"))
            or wet_lobe_scale["corrected_lobe_mean"] <= 0
            or not number(wet_lobe_scale.get("corrected_lobe_max"))
            or wet_lobe_scale["corrected_lobe_max"]
            < wet_lobe_scale["corrected_lobe_mean"]
            or not number(
                wet_lobe_scale.get("maximum_absolute_residual")
            )
            or wet_lobe_scale["maximum_absolute_residual"] > 1.0e-5
            or not number(
                wet_lobe_scale.get("maximum_relative_residual")
            )
            or wet_lobe_scale["maximum_relative_residual"] > 1.0e-4
            or wet_lobe_scale.get("violations") != []
            or wet_lobe_scale.get("verdict") != "PASS"
        ):
            fail("property:wet_lobe_scale")

        ambient_layering = properties.get("ambient_ibl_layering")
        ambient_levels = (
            ambient_layering.get("levels")
            if isinstance(ambient_layering, dict)
            else None
        )
        recovered_reference: list[float] | None = None
        if ambient_layering is not None and (
            not isinstance(ambient_layering, dict)
            or ambient_layering.get("diffuse_formula")
            != "Dwet=Dstock*(1-ambientWetnessF)"
            or ambient_layering.get("specular_formula")
            != "Swet=Sstock*(1-ambientWetnessF)+film*ambientWetnessF"
            or not isinstance(ambient_levels, list)
            or len(ambient_levels) != len(requested_levels)
            or ambient_layering.get("violations") != []
            or ambient_layering.get("verdict") != "PASS"
        ):
            fail("property:ambient_ibl_layering")
        elif isinstance(ambient_levels, list):
            for index, level in enumerate(ambient_levels):
                arrays = {
                    name: level.get(name)
                    for name in (
                        "substrate_diffuse",
                        "layered_diffuse",
                        "substrate_specular",
                        "layered_specular",
                        "recovered_film",
                    )
                } if isinstance(level, dict) else {}
                if (
                    not isinstance(level, dict)
                    or not close(
                        level.get("requested"), requested_levels[index]
                    )
                    or not close(
                        level.get("uploaded"),
                        uploaded_levels[index],
                        1.0e-7,
                    )
                    or not number(level.get("attenuation_mean"))
                    or not (-1.0e-5 <= level["attenuation_mean"] <= 1.00001)
                    or not number(
                        level.get("representative_attenuation")
                    )
                    or not (
                        -1.0e-5
                        <= level["representative_attenuation"]
                        <= 1.00001
                    )
                    or any(
                        not isinstance(values, list)
                        or len(values) != 3
                        or any(not number(value) for value in values)
                        for values in arrays.values()
                    )
                    or not number(
                        level.get("maximum_diffuse_factor_spread")
                    )
                    or level["maximum_diffuse_factor_spread"] > 1.0e-5
                    or not number(level.get("maximum_film_residual"))
                    or level["maximum_film_residual"]
                    > 2.0e-5 + 2.0e-4
                    or not number(
                        level.get("maximum_untouched_mutant_residual")
                    )
                    or level["maximum_untouched_mutant_residual"] > 1.0e-5
                ):
                    fail("ambient_ibl_layering:level")
                    continue
                attenuation = float(
                    level["representative_attenuation"]
                )
                for channel in range(3):
                    expected_diffuse = (
                        float(arrays["substrate_diffuse"][channel]) *
                        attenuation
                    )
                    if not close(
                        arrays["layered_diffuse"][channel],
                        expected_diffuse,
                        1.0e-4,
                    ):
                        fail("ambient_ibl_layering:diffuse")
                        break
                    expected_specular = (
                        float(arrays["substrate_specular"][channel]) *
                        attenuation
                        + float(arrays["recovered_film"][channel]) *
                        (1.0 - attenuation)
                    )
                    if not close(
                        arrays["layered_specular"][channel],
                        expected_specular,
                        1.0e-4,
                    ):
                        fail("ambient_ibl_layering:specular")
                        break
                if index == 0:
                    if (
                        not close(attenuation, 1.0, 1.0e-5)
                        or any(
                            not close(value, 0.0, 1.0e-8)
                            for value in arrays["recovered_film"]
                        )
                    ):
                        fail("ambient_ibl_layering:identity")
                else:
                    if (
                        attenuation >= 1.0
                        or any(
                            value <= 0
                            for value in arrays["recovered_film"]
                        )
                    ):
                        fail("ambient_ibl_layering:film")
                    if recovered_reference is None:
                        recovered_reference = [
                            float(value)
                            for value in arrays["recovered_film"]
                        ]
                    elif any(
                        not close(value, reference, 5.0e-4)
                        for value, reference in zip(
                            arrays["recovered_film"],
                            recovered_reference,
                        )
                    ):
                        fail("ambient_ibl_layering:film_consistency")

        monotonicity = properties.get("monotonicity")
        series = (
            monotonicity.get("series")
            if isinstance(monotonicity, dict)
            else None
        )
        if (
            not isinstance(monotonicity, dict)
            or monotonicity.get("claim")
            != "isolated diffuse absolute RGB delta is nondecreasing"
            or monotonicity.get("violations") != 0
            or monotonicity.get("verdict") != "PASS"
            or not isinstance(series, list)
            or [item.get("scenario") for item in series if isinstance(item, dict)]
            != target_contract["scenario_buckets"]
        ):
            fail("property:monotonicity")
        elif len(series) == len(target_contract["scenario_buckets"]):
            for item in series:
                levels = item.get("levels")
                claims_monotonic = item.get("scenario") == "diffuse"
                expected_claim = (
                    "absolute RGB delta is nondecreasing"
                    if claims_monotonic
                    else "net layered delta measured; monotonicity not claimed"
                )
                if (
                    item.get("violations") != 0
                    or item.get("verdict") != "PASS"
                    or item.get("claim") != expected_claim
                    or not isinstance(levels, list)
                    or len(levels) != len(requested_levels)
                ):
                    fail("monotonicity:series")
                    continue
                previous = -1.0
                for index, level in enumerate(levels):
                    energy = (
                        level.get("delta_energy")
                        if isinstance(level, dict)
                        else None
                    )
                    if (
                        not isinstance(level, dict)
                        or not close(
                            level.get("requested"),
                            requested_levels[index],
                        )
                        or not close(
                            level.get("uploaded"),
                            uploaded_levels[index],
                            1.0e-7,
                        )
                        or not number(energy)
                        or energy < 0
                        or (
                            claims_monotonic
                            and energy + 1.0e-12 < previous
                        )
                        or (index == 0 and not close(energy, 0.0))
                    ):
                        fail("monotonicity:levels")
                        break
                    previous = float(energy)
        mutation = properties.get("mutation_sensitivity")
        if (
            not isinstance(mutation, dict)
            or mutation.get("id")
            != target_contract["mutation"]["id"]
            or mutation.get("expected_failed_property")
            != target_contract["mutation"]["expected_failed_property"]
            or mutation.get("observed_failed_properties")
            != ["matte_sheen", "no_ibl_film", "ambient_film_blend"]
            or mutation.get("neutral_identity") != "PASS"
            or mutation.get("matte_sheen") != "FAIL"
            or mutation.get("no_ibl_film") != "FAIL"
            or mutation.get("ambient_film_blend") != "FAIL"
            or mutation.get("glossy_reflection") != "PASS"
            or mutation.get("verdict") != "CAUGHT"
        ):
            fail("property:mutation_sensitivity")
        else:
            mutation_matte = mutation.get("matte_probe")
            mutation_no_ibl = mutation.get("no_ibl_probe")
            mutation_layered = mutation.get("layered_probe")
            mutation_glossy = mutation.get("glossy_probe")
            full_population = expected_bucket_evidence["mask.full"][
                "population"
            ]
            matte_absolute = (
                mutation_matte.get("absolute_delta")
                if isinstance(mutation_matte, dict)
                else None
            )
            matte_signed = (
                mutation_matte.get("signed_delta")
                if isinstance(mutation_matte, dict)
                else None
            )
            if (
                not isinstance(mutation_matte, dict)
                or mutation_matte.get("name") != "color"
                or mutation_matte.get("population") != full_population
                or mutation_matte.get("changed_pixels") != 0
                or mutation_matte.get("changed_channels") != 0
                or mutation_matte.get("stock_energy") != 0
                or mutation_matte.get("delta_energy") != 0
                or mutation_matte.get("delta_fraction_of_stock") != 0
                or mutation_matte.get("energy_verdict") != "UNPROVEN"
                or not isinstance(matte_absolute, dict)
                or any(value != 0 for value in matte_absolute.values())
                or not isinstance(matte_signed, dict)
                or any(value != 0 for value in matte_signed.values())
            ):
                fail("mutation_sensitivity:matte_probe")
            no_ibl_signed = (
                mutation_no_ibl.get("signed_delta")
                if isinstance(mutation_no_ibl, dict)
                else None
            )
            if (
                not isinstance(mutation_no_ibl, dict)
                or mutation_no_ibl.get("name") != "color"
                or mutation_no_ibl.get("population") != full_population
                or mutation_no_ibl.get("changed_pixels") != full_population
                or mutation_no_ibl.get("changed_channels")
                != full_population * 3
                or not number(mutation_no_ibl.get("stock_energy"))
                or mutation_no_ibl["stock_energy"] <= 0
                or not number(mutation_no_ibl.get("delta_energy"))
                or mutation_no_ibl["delta_energy"] <= 0
                or not number(
                    mutation_no_ibl.get("delta_fraction_of_stock")
                )
                or not (
                    0
                    < mutation_no_ibl["delta_fraction_of_stock"]
                    <= 1.01
                )
                or mutation_no_ibl.get("energy_verdict") != "PROVEN"
                or not isinstance(no_ibl_signed, dict)
                or not number(no_ibl_signed.get("max"))
                or no_ibl_signed["max"] >= 0
            ):
                fail("mutation_sensitivity:no_ibl_probe")
            layered_signed = (
                mutation_layered.get("signed_delta")
                if isinstance(mutation_layered, dict)
                else None
            )
            if (
                not isinstance(mutation_layered, dict)
                or mutation_layered.get("name") != "color"
                or mutation_layered.get("population") != width * height
                or mutation_layered.get("changed_pixels") != width * height
                or mutation_layered.get("changed_channels")
                != width * height * 3
                or not number(mutation_layered.get("delta_energy"))
                or mutation_layered["delta_energy"] <= 0
                or not isinstance(layered_signed, dict)
                or not number(layered_signed.get("max"))
                or layered_signed["max"] >= 0
            ):
                fail("mutation_sensitivity:layered_probe")
            glossy_signed = (
                mutation_glossy.get("signed_delta")
                if isinstance(mutation_glossy, dict)
                else None
            )
            if (
                not isinstance(mutation_glossy, dict)
                or mutation_glossy.get("name") != "color"
                or mutation_glossy.get("population") != full_population
                or mutation_glossy.get("changed_pixels") != full_population
                or not number(mutation_glossy.get("delta_energy"))
                or mutation_glossy["delta_energy"] <= 0
                or not number(
                    mutation_glossy.get("delta_fraction_of_stock")
                )
                or not (
                    0
                    < mutation_glossy["delta_fraction_of_stock"]
                    <= 1.01
                )
                or mutation_glossy.get("energy_verdict") != "PROVEN"
                or not isinstance(glossy_signed, dict)
                or not number(glossy_signed.get("min"))
                or glossy_signed["min"] <= 0
            ):
                fail("mutation_sensitivity:glossy_probe")

    buckets = measurement.get("cross_buckets")
    expected_keys = [
        (mask, scenario)
        for scenario in target_contract["scenario_buckets"]
        for mask in feature_contract["mask_buckets"]
    ]
    observed_keys: list[tuple[str, str]] = []
    bucket_by_key: dict[tuple[str, str], dict[str, Any]] = {}
    populations: dict[str, set[int]] = {
        name: set() for name in feature_contract["mask_buckets"]
    }
    ranges: dict[str, set[tuple[float, float]]] = {
        name: set() for name in feature_contract["mask_buckets"]
    }
    if not isinstance(buckets, list):
        fail("cross_buckets")
    else:
        for bucket in buckets:
            if not isinstance(bucket, dict):
                fail("cross_bucket")
                continue
            key = (str(bucket.get("mask")), str(bucket.get("scenario")))
            observed_keys.append(key)
            bucket_by_key[key] = bucket
            population = bucket.get("population")
            evidence = expected_bucket_evidence.get(key[0])
            if (
                bucket.get("fixture") != fixture
                or not isinstance(population, int)
                or isinstance(population, bool)
                or population < feature_contract["minimum_bucket_population"]
                or population > width * height
                or evidence is None
                or population != evidence["population"]
            ):
                fail("cross_bucket_population")
                population = 0
            if key[0] in populations and population:
                populations[key[0]].add(population)
            post_upload = bucket.get("post_upload")
            if (
                not isinstance(post_upload, dict)
                or not number(post_upload.get("minimum"))
                or not number(post_upload.get("maximum"))
                or post_upload["minimum"] > post_upload["maximum"]
            ):
                fail("post_upload")
            elif evidence is not None:
                minimum = float(post_upload["minimum"])
                maximum = float(post_upload["maximum"])
                if (
                    not close(minimum, evidence["minimum"], 1.0e-7)
                    or not close(maximum, evidence["maximum"], 1.0e-7)
                ):
                    fail("post_upload_expected_range")
                if key[0] in ranges:
                    ranges[key[0]].add((minimum, maximum))

            output = bucket.get("output")
            distribution = (
                output.get("absolute_delta")
                if isinstance(output, dict)
                else None
            )
            signed_distribution = (
                output.get("signed_delta")
                if isinstance(output, dict)
                else None
            )
            numeric_fields = (
                "population",
                "changed_pixels",
                "changed_channels",
                "stock_energy",
                "delta_energy",
                "denominator_threshold",
                "delta_fraction_of_stock",
            )
            expected_energy_verdict = (
                "UNPROVEN"
                if key[1] == "matte"
                else "PROVEN"
            )
            if (
                not isinstance(output, dict)
                or output.get("name") != "color"
                or any(
                    not number(output.get(name)) or output[name] < 0
                    for name in numeric_fields
                )
                or not isinstance(output.get("population"), int)
                or not isinstance(output.get("changed_pixels"), int)
                or not isinstance(output.get("changed_channels"), int)
                or output["population"] != population
                or output["changed_pixels"] > population
                or output["changed_channels"] > population * 3
                or output.get("energy_verdict") != expected_energy_verdict
                or not isinstance(distribution, dict)
                or any(
                    not number(distribution.get(name))
                    or distribution[name] < 0
                    for name in ("min", "mean", "p50", "p95", "p99", "max")
                )
                or not isinstance(signed_distribution, dict)
                or any(
                    not number(signed_distribution.get(name))
                    for name in ("min", "mean", "p05", "p50", "p95", "max")
                )
            ):
                fail("output_stats")
                continue
            ordered = [
                distribution["min"],
                distribution["p50"],
                distribution["p95"],
                distribution["p99"],
                distribution["max"],
            ]
            if (
                ordered != sorted(ordered)
                or not (
                    distribution["min"]
                    <= distribution["mean"]
                    <= distribution["max"]
                )
            ):
                fail("output_distribution_order")
            signed_ordered = [
                signed_distribution["min"],
                signed_distribution["p05"],
                signed_distribution["p50"],
                signed_distribution["p95"],
                signed_distribution["max"],
            ]
            if (
                signed_ordered != sorted(signed_ordered)
                or not (
                    signed_distribution["min"]
                    <= signed_distribution["mean"]
                    <= signed_distribution["max"]
                )
            ):
                fail("output_signed_distribution_order")
            channel_count = population * 3
            if not close(
                distribution["mean"] * channel_count,
                output["delta_energy"],
                1.0e-8,
            ):
                fail("output_mean_energy")
            expected_threshold = max(1.0e-12, population * 3.0e-12)
            if not close(
                output["denominator_threshold"],
                expected_threshold,
                1.0e-12,
            ):
                fail("output_energy_fraction")
            elif key[1] == "matte":
                if (
                    output["stock_energy"] > output["denominator_threshold"]
                    or output["delta_fraction_of_stock"] != 0
                ):
                    fail("output_energy_fraction")
            elif (
                output["stock_energy"] <= output["denominator_threshold"]
                or not close(
                    output["delta_fraction_of_stock"],
                    output["delta_energy"] / output["stock_energy"],
                    1.0e-8,
                )
            ):
                fail("output_energy_fraction")
            if key[0] == "mask.zero":
                if (
                    output["changed_pixels"] != 0
                    or output["changed_channels"] != 0
                    or output["delta_energy"] != 0
                    or output["delta_fraction_of_stock"] != 0
                    or any(value != 0 for value in distribution.values())
                    or any(
                        value != 0
                        for value in signed_distribution.values()
                    )
                ):
                    fail("output_expected_zero")
            elif (
                (
                    key[0] == "mask.full"
                    and output["changed_pixels"] != population
                )
                or output["changed_channels"] < output["changed_pixels"]
                or (
                    key[0] == "mask.full"
                    and output["delta_energy"] <= 0
                )
            ):
                fail("output_active_locality")
            if (
                key[0] != "mask.zero"
                and key[1] == "diffuse"
                and signed_distribution["max"] >= 0
            ):
                fail("output_diffuse_direction")
        if observed_keys != expected_keys or len(buckets) != len(expected_keys):
            fail("cross_bucket_matrix")
        for mask in feature_contract["mask_buckets"]:
            if len(populations[mask]) != 1 or len(ranges[mask]) != 1:
                fail(f"cross_bucket_consistency:{mask}")
        if isinstance(monotonicity, dict):
            series = monotonicity.get("series")
            if isinstance(series, list):
                for item in series:
                    if not isinstance(item, dict):
                        continue
                    levels = item.get("levels")
                    full = bucket_by_key.get(
                        ("mask.full", str(item.get("scenario")))
                    )
                    if (
                        not isinstance(levels, list)
                        or not levels
                        or not isinstance(levels[-1], dict)
                        or full is None
                        or not isinstance(full.get("output"), dict)
                        or not number(levels[-1].get("delta_energy"))
                        or not number(full["output"].get("delta_energy"))
                        or levels[-1]["delta_energy"]
                        + 1.0e-8
                        < full["output"]["delta_energy"]
                    ):
                        fail("monotonicity:cross_evidence")

    reflection = bucket_by_key.get(("mask.full", "reflection"))
    reflection_output = (
        reflection.get("output") if isinstance(reflection, dict) else None
    )
    reflection_absolute = (
        reflection_output.get("absolute_delta")
        if isinstance(reflection_output, dict)
        else None
    )
    reflection_signed = (
        reflection_output.get("signed_delta")
        if isinstance(reflection_output, dict)
        else None
    )
    if (
        not isinstance(reflection_output, dict)
        or reflection_output.get("changed_pixels")
        != reflection_output.get("population")
        or reflection_output.get("delta_energy", 0) <= 0
        or reflection_output.get("energy_verdict") != "PROVEN"
        or reflection_output.get("stock_energy", 0)
        <= reflection_output.get("denominator_threshold", 0)
        or reflection_output.get("delta_fraction_of_stock", 2.51) > 2.5
        or not isinstance(reflection_absolute, dict)
        or reflection_absolute.get("p95", 0.91) > 0.9
        or reflection_absolute.get("max", 1.01) > 1.0
        or not isinstance(reflection_signed, dict)
        or reflection_signed.get("max", 1.01) > 1.0
    ):
        fail("magnitude:glossy_probe")
    matte = bucket_by_key.get(("mask.full", "matte"))
    matte_output = (
        matte.get("output") if isinstance(matte, dict) else None
    )
    matte_signed = (
        matte_output.get("signed_delta")
        if isinstance(matte_output, dict)
        else None
    )
    if (
        not isinstance(matte_output, dict)
        or matte_output.get("changed_pixels") != matte_output.get("population")
        or matte_output.get("changed_channels")
        != matte_output.get("population", 0) * 3
        or matte_output.get("delta_energy", 0) <= 0
        or matte_output.get("stock_energy") != 0
        or matte_output.get("delta_fraction_of_stock") != 0
        or matte_output.get("energy_verdict") != "UNPROVEN"
        or not isinstance(matte_signed, dict)
        or matte_signed.get("min", 0) <= 0
    ):
        fail("matte_sheen:probe")
    no_ibl = bucket_by_key.get(("mask.full", "no-ibl"))
    no_ibl_output = (
        no_ibl.get("output") if isinstance(no_ibl, dict) else None
    )
    no_ibl_signed = (
        no_ibl_output.get("signed_delta")
        if isinstance(no_ibl_output, dict)
        else None
    )
    if (
        not isinstance(no_ibl_output, dict)
        or no_ibl_output.get("changed_pixels")
        != no_ibl_output.get("population")
        or no_ibl_output.get("changed_channels")
        != no_ibl_output.get("population", 0) * 3
        or no_ibl_output.get("delta_energy", 0) <= 0
        or no_ibl_output.get("stock_energy", 0)
        <= no_ibl_output.get("denominator_threshold", 0)
        or no_ibl_output.get("delta_fraction_of_stock", 0) <= 0
        or no_ibl_output.get("energy_verdict") != "PROVEN"
        or not isinstance(no_ibl_signed, dict)
        or no_ibl_signed.get("min", 0) <= 0
    ):
        fail("no_ibl_film:probe")
    if measurement.get("verdict") != "PASS":
        fail("verdict")
    if _has_absolute_path(measurement):
        fail("absolute_path")
    return failures


def validate_feature_measurement(
    measurement: Any,
    fixture: str,
    feature_contract: dict[str, Any],
    width: int,
    height: int,
    expected_source_sha256: str,
) -> list[OrderedDict[str, str]]:
    if (
        isinstance(measurement, dict)
        and measurement.get("target") == "ambient-runtime"
    ):
        return _validate_ambient_feature_measurement(
            measurement,
            fixture,
            feature_contract,
            width,
            height,
            expected_source_sha256,
        )
    failures: list[OrderedDict[str, str]] = []

    def fail(detail: str) -> None:
        failures.append(
            _feature_failure_record(
                "feature_measurement_protocol", detail
            )
        )

    def number(value: Any) -> bool:
        return (
            isinstance(value, (int, float))
            and not isinstance(value, bool)
            and math.isfinite(float(value))
        )

    def close(left: Any, right: Any, tolerance: float = 1.0e-9) -> bool:
        return (
            number(left)
            and number(right)
            and math.isclose(
                float(left),
                float(right),
                rel_tol=tolerance,
                abs_tol=tolerance,
            )
        )

    if (
        not isinstance(measurement, dict)
        or measurement.get("schema") != FEATURE_MEASUREMENT_SCHEMA
        or measurement.get("schema_version") != 1
    ):
        fail("schema")
        return failures
    expected_scalars = {
        "harness_version": HARNESS_VERSION,
        "source_sha256": expected_source_sha256,
        "evidence_class": "additive-feature",
        "comparison": "reconstructed-stock-vs-reconstructed-feature",
        "native_bytecode_used": False,
        "target": "directional",
        "profile": feature_contract["profile"],
        "fixture": fixture,
        "width": width,
        "height": height,
        "measurement_protocol": feature_contract["measurement_protocol"],
        "wetness_format": next(
            item["wetness_format"]
            for item in feature_contract["fixtures"]
            if item["id"] == fixture
        ),
    }
    for name, expected in expected_scalars.items():
        if measurement.get(name) != expected:
            fail(name)
    raw_harness_failures = measurement.get("failures")
    if raw_harness_failures:
        if (
            not isinstance(raw_harness_failures, list)
            or any(
                not isinstance(item, dict)
                or item.get("schema") != FEATURE_MEASUREMENT_SCHEMA
                or item.get("schema_version") != 1
                or item.get("evidence_class") != "additive-feature"
                or item.get("comparison")
                != "reconstructed-stock-vs-reconstructed-feature"
                or item.get("native_bytecode_used") is not False
                or not isinstance(item.get("code"), str)
                or not isinstance(item.get("detail"), str)
                for item in raw_harness_failures
            )
        ):
            fail("failure_receipt")
        else:
            for item in raw_harness_failures:
                failures.append(
                    _feature_failure_record(item["code"], item["detail"])
                )
        if measurement.get("verdict") != "UNPROVEN":
            fail("failure_verdict")
        if _has_absolute_path(measurement):
            fail("absolute_path")
        return failures
    if raw_harness_failures is not None and raw_harness_failures != []:
        fail("failures")

    if measurement.get("measurement_format") != "R32G32B32A32_FLOAT":
        fail("measurement_format")
    formats = measurement.get("formats")
    if not isinstance(formats, list) or not formats:
        fail("formats")
    else:
        wetness_formats = [
            item
            for item in formats
            if isinstance(item, dict) and item.get("bind_point") == 4
        ]
        if (
            len(wetness_formats) != 1
            or wetness_formats[0].get("dimension") != "texture2d"
            or wetness_formats[0].get("resource_format")
            != expected_scalars["wetness_format"]
            or wetness_formats[0].get("srv_format")
            != expected_scalars["wetness_format"]
        ):
            fail("wetness_format_record")
    environment = measurement.get("execution_environment")
    if (
        not isinstance(environment, dict)
        or environment.get("driver_type") != "WARP"
        or environment.get("feature_level") != "11_0"
        or environment.get("native_bytecode_used") is not False
    ):
        fail("execution_environment")
    seeds = measurement.get("seeds")
    if (
        not isinstance(seeds, list)
        or len(seeds) != 2
        or any(not isinstance(seed, int) for seed in seeds)
    ):
        fail("seeds")
    generated = measurement.get("generated_inputs_sha256")
    if not isinstance(generated, str) or re.fullmatch(r"[0-9a-f]{64}", generated) is None:
        fail("generated_inputs_sha256")
    hashes = measurement.get("hashes")
    expected_mask_ids = [
        "active-pattern",
        "level-0",
        "level-1",
        "level-2",
        "level-3",
        "level-4",
        "neutral-zero",
    ]
    expected_mask_payloads = _expected_wetness_mask_payloads(
        fixture,
        width,
        height,
        feature_contract["monotonic_levels"],
    )
    expected_bucket_evidence = _wetness_bucket_evidence_from_payload(
        fixture, expected_mask_payloads["active-pattern"]
    )
    mask_records: dict[str, list[dict[str, Any]]] = {}
    if not isinstance(hashes, dict):
        fail("hashes")
    else:
        for name in ("uploaded_masks", "readback_masks"):
            records = hashes.get(name)
            if not isinstance(records, list):
                fail(name)
                continue
            if any(not isinstance(item, dict) for item in records):
                fail(f"{name}_record")
                continue
            labels = [item.get("id") for item in records]
            if labels != expected_mask_ids:
                fail(f"{name}_order")
            for item in records:
                expected_payload = expected_mask_payloads.get(item.get("id"))
                if (
                    expected_payload is None
                    or re.fullmatch(
                        r"[0-9a-f]{64}", str(item.get("sha256", ""))
                    )
                    is None
                    or not isinstance(item.get("size"), int)
                    or isinstance(item.get("size"), bool)
                    or item["size"] != len(expected_payload or b"")
                    or item.get("sha256")
                    != hashlib.sha256(expected_payload or b"").hexdigest()
                ):
                    fail(f"{name}_record")
                    break
            mask_records[name] = records
        uploaded = mask_records.get("uploaded_masks")
        readback = mask_records.get("readback_masks")
        if uploaded is not None and readback is not None:
            for uploaded_item, readback_item in zip(uploaded, readback):
                if (
                    uploaded_item.get("id") != readback_item.get("id")
                    or uploaded_item.get("size") != readback_item.get("size")
                    or uploaded_item.get("sha256")
                    != readback_item.get("sha256")
                ):
                    fail("mask_hash_pair")
                    break

    contract_delta = measurement.get("contract_delta")
    if (
        not isinstance(contract_delta, dict)
        or contract_delta.get("stock_to_feature") != "only-texture2d-t4-added"
        or contract_delta.get("mutation_t4_optimization_away_allowed") is not False
        or contract_delta.get("verdict") != "PASS"
    ):
        fail("contract_delta")
    if measurement.get("variants") != feature_contract["variants"]:
        fail("variants")

    monotonicity: Any = None
    properties = measurement.get("properties")
    if not isinstance(properties, dict):
        fail("properties")
    else:
        neutral = properties.get("neutral_identity")
        if (
            not isinstance(neutral, dict)
            or neutral.get("tolerance_absolute") != 0
            or neutral.get("tolerance_relative") != 0
            or neutral.get("comparisons") != 12
            or neutral.get("violations") != []
            or neutral.get("verdict") != "PASS"
        ):
            fail("property:neutral_identity")

        locality = properties.get("active_locality")
        if (
            not isinstance(locality, dict)
            or locality.get("zero_tolerance") is not True
            or locality.get("bucket_basis") != (
                "t4 GPU readback after fixture quantization; "
                "material from post-quantization upload"
            )
            or locality.get("violations") != []
            or locality.get("verdict") != "PASS"
        ):
            fail("property:active_locality")
        else:
            pattern = locality["pattern"] if "pattern" in locality else None
            if not isinstance(pattern, dict) or pattern != {
                "exact_zero": True,
                "exact_one": True,
                "smooth_strict_partial_ramp": True,
                "isolated_full_patch_with_zero_moat": True,
            }:
                fail("active_locality:pattern")

        magnitude = properties.get("magnitude")
        if (
            not isinstance(magnitude, dict)
            or magnitude.get("rgb_only") is not True
            or magnitude.get("invalid_denominator_buckets") != 0
            or magnitude.get("verdict") != "PASS"
        ):
            fail("property:magnitude")

        requested_levels = [
            float(value) for value in feature_contract["monotonic_levels"]
        ]
        uploaded_levels = [
            (
                math.floor(value * 255.0 + 0.5) / 255.0
                if fixture == "native"
                else value
            )
            for value in requested_levels
        ]

        wet_lobe_scale = properties.get("wet_lobe_scale")
        if (
            not isinstance(wet_lobe_scale, dict)
            or wet_lobe_scale.get("stock_scale_symbol")
            != "FO4_DIRECTIONAL_SPECULAR_SCALE"
            or wet_lobe_scale.get("film_scale_symbol")
            != "FO4_DIRECTIONAL_SPECULAR_SCALE"
            or wet_lobe_scale.get("stock_scale") != 3.141593
            or wet_lobe_scale.get("expected_derating_ratio") != 0.65
            or wet_lobe_scale.get("expected_difference_ratio") != 0.35
            or not isinstance(
                wet_lobe_scale.get("proven_channels"), int
            )
            or wet_lobe_scale["proven_channels"] <= 0
            or not number(wet_lobe_scale.get("corrected_lobe_mean"))
            or wet_lobe_scale["corrected_lobe_mean"] <= 0
            or not number(wet_lobe_scale.get("corrected_lobe_max"))
            or wet_lobe_scale["corrected_lobe_max"]
            < wet_lobe_scale["corrected_lobe_mean"]
            or not number(
                wet_lobe_scale.get("maximum_absolute_residual")
            )
            or wet_lobe_scale["maximum_absolute_residual"] > 1.0e-5
            or not number(
                wet_lobe_scale.get("maximum_relative_residual")
            )
            or wet_lobe_scale["maximum_relative_residual"] > 1.0e-4
            or wet_lobe_scale.get("violations") != []
            or wet_lobe_scale.get("verdict") != "PASS"
        ):
            fail("property:wet_lobe_scale")

        ambient_layering = properties.get("ambient_ibl_layering")
        ambient_levels = (
            ambient_layering.get("levels")
            if isinstance(ambient_layering, dict)
            else None
        )
        recovered_reference: list[float] | None = None
        if (
            not isinstance(ambient_layering, dict)
            or ambient_layering.get("diffuse_formula")
            != "Dwet=Dstock*(1-ambientWetnessF)"
            or ambient_layering.get("specular_formula")
            != "Swet=Sstock*(1-ambientWetnessF)+film*ambientWetnessF"
            or not isinstance(ambient_levels, list)
            or len(ambient_levels) != len(requested_levels)
            or ambient_layering.get("violations") != []
            or ambient_layering.get("verdict") != "PASS"
        ):
            fail("property:ambient_ibl_layering")
        elif isinstance(ambient_levels, list):
            for index, level in enumerate(ambient_levels):
                arrays = {
                    name: level.get(name)
                    for name in (
                        "substrate_diffuse",
                        "layered_diffuse",
                        "substrate_specular",
                        "layered_specular",
                        "recovered_film",
                    )
                } if isinstance(level, dict) else {}
                if (
                    not isinstance(level, dict)
                    or not close(
                        level.get("requested"), requested_levels[index]
                    )
                    or not close(
                        level.get("uploaded"),
                        uploaded_levels[index],
                        1.0e-7,
                    )
                    or not number(level.get("attenuation_mean"))
                    or not (-1.0e-5 <= level["attenuation_mean"] <= 1.00001)
                    or not number(
                        level.get("representative_attenuation")
                    )
                    or not (
                        -1.0e-5
                        <= level["representative_attenuation"]
                        <= 1.00001
                    )
                    or any(
                        not isinstance(values, list)
                        or len(values) != 3
                        or any(not number(value) for value in values)
                        for values in arrays.values()
                    )
                    or not number(
                        level.get("maximum_diffuse_factor_spread")
                    )
                    or level["maximum_diffuse_factor_spread"] > 1.0e-5
                    or not number(level.get("maximum_film_residual"))
                    or level["maximum_film_residual"] > 5.0e-4
                    or not number(
                        level.get("maximum_untouched_mutant_residual")
                    )
                    or level["maximum_untouched_mutant_residual"] > 1.0e-5
                ):
                    fail("ambient_ibl_layering:level")
                    continue
                attenuation = float(
                    level["representative_attenuation"]
                )
                for channel in range(3):
                    expected_diffuse = (
                        float(arrays["substrate_diffuse"][channel]) *
                        attenuation
                    )
                    if not close(
                        arrays["layered_diffuse"][channel],
                        expected_diffuse,
                        1.0e-4,
                    ):
                        fail("ambient_ibl_layering:diffuse")
                        break
                    expected_specular = (
                        float(arrays["substrate_specular"][channel]) *
                        attenuation
                        + float(arrays["recovered_film"][channel]) *
                        (1.0 - attenuation)
                    )
                    if not close(
                        arrays["layered_specular"][channel],
                        expected_specular,
                        1.0e-4,
                    ):
                        fail("ambient_ibl_layering:specular")
                        break
                if index == 0:
                    if (
                        not close(attenuation, 1.0, 1.0e-5)
                        or any(
                            not close(value, 0.0, 1.0e-8)
                            for value in arrays["recovered_film"]
                        )
                    ):
                        fail("ambient_ibl_layering:identity")
                else:
                    if (
                        attenuation >= 1.0
                        or any(
                            value <= 0
                            for value in arrays["recovered_film"]
                        )
                    ):
                        fail("ambient_ibl_layering:film")
                    if recovered_reference is None:
                        recovered_reference = [
                            float(value)
                            for value in arrays["recovered_film"]
                        ]
                    elif any(
                        not close(value, reference, 5.0e-4)
                        for value, reference in zip(
                            arrays["recovered_film"],
                            recovered_reference,
                        )
                    ):
                        fail("ambient_ibl_layering:film_consistency")

        def validate_levels(
            levels: Any,
            energy_name: str,
            detail: str,
        ) -> None:
            if not isinstance(levels, list) or len(levels) != 5:
                fail(detail)
                return
            previous_energy = -1.0
            for index, level in enumerate(levels):
                if (
                    not isinstance(level, dict)
                    or not close(level.get("requested"), requested_levels[index])
                    or not close(
                        level.get("uploaded"),
                        uploaded_levels[index],
                        1.0e-7,
                    )
                    or not number(level.get(energy_name))
                    or level[energy_name] < 0
                    or level[energy_name] + 1.0e-12 < previous_energy
                ):
                    fail(detail)
                    return
                if index == 0 and not close(level[energy_name], 0.0):
                    fail(detail)
                    return
                previous_energy = float(level[energy_name])

        monotonicity = properties.get("monotonicity")
        if (
            not isinstance(monotonicity, dict)
            or monotonicity.get("direct_specular_claim") != "not-claimed"
            or monotonicity.get("violations") != 0
            or monotonicity.get("verdict") != "PASS"
        ):
            fail("property:monotonicity")
        else:
            diffuse_series = monotonicity.get("diffuse_series")
            expected_series = [
                ("material.default", "inactive"),
                ("material.default", "active"),
                ("material.skin", "inactive"),
                ("material.skin", "active"),
            ]
            if (
                not isinstance(diffuse_series, list)
                or len(diffuse_series) != len(expected_series)
            ):
                fail("monotonicity:diffuse_series")
            else:
                for series, (material, ibl) in zip(
                    diffuse_series, expected_series
                ):
                    if (
                        not isinstance(series, dict)
                        or series.get("material") != material
                        or series.get("ibl") != ibl
                        or series.get("violations") != 0
                        or series.get("verdict") != "PASS"
                    ):
                        fail("monotonicity:diffuse_series")
                        continue
                    validate_levels(
                        series.get("levels"),
                        "diffuse_delta_energy",
                        "monotonicity:diffuse_levels",
                    )
            probe = monotonicity.get("ibl_specular_probe")
            if (
                not isinstance(probe, dict)
                or probe.get("scope")
                != "paired directional-IBL activity"
                or probe.get("claim")
                != "ambient gradient wetness delta is measured"
                or probe.get("violations") != 0
                or probe.get("verdict") != "PASS"
            ):
                fail("monotonicity:ibl_specular_probe")
            else:
                probe_levels = probe.get("levels")
                if (
                    not isinstance(probe_levels, list)
                    or len(probe_levels) != len(requested_levels)
                ):
                    fail("monotonicity:ibl_specular_levels")
                else:
                    for index, level in enumerate(probe_levels):
                        if (
                            not isinstance(level, dict)
                            or not close(
                                level.get("requested"),
                                requested_levels[index],
                            )
                            or not close(
                                level.get("uploaded"),
                                uploaded_levels[index],
                                1.0e-7,
                            )
                            or not number(level.get("delta_energy"))
                            or level["delta_energy"] < 0
                            or (
                                index == 0
                                and level["delta_energy"] > 1.0e-6
                            )
                            or (
                                index != 0
                                and level["delta_energy"] <= 0
                            )
                        ):
                            fail("monotonicity:ibl_specular_levels")
                            break
            if isinstance(diffuse_series, list) and isinstance(probe, dict):
                violation_values = [
                    series.get("violations")
                    for series in diffuse_series
                    if isinstance(series, dict)
                ]
                violation_values.append(probe.get("violations"))
                if (
                    len(violation_values) != 5
                    or any(
                        not isinstance(value, int) or isinstance(value, bool)
                        for value in violation_values
                    )
                    or sum(violation_values) != monotonicity.get("violations")
                ):
                    fail("monotonicity:violation_total")

        mutation = properties.get("mutation_sensitivity")
        mutants = (
            mutation.get("mutants")
            if isinstance(mutation, dict)
            else None
        )
        old_mutant = (
            mutants[0]
            if isinstance(mutants, list) and len(mutants) == 3
            else None
        )
        pi_mutant = (
            mutants[1]
            if isinstance(mutants, list) and len(mutants) == 3
            else None
        )
        ambient_mutant = (
            mutants[2]
            if isinstance(mutants, list) and len(mutants) == 3
            else None
        )
        mutation_diffuse = (
            old_mutant.get("diffuse_probe")
            if isinstance(old_mutant, dict)
            else None
        )
        mutation_specular = (
            old_mutant.get("specular_probe")
            if isinstance(old_mutant, dict)
            else None
        )
        mutation_diffuse_signed = (
            mutation_diffuse.get("signed_delta")
            if isinstance(mutation_diffuse, dict)
            else None
        )
        if (
            not isinstance(mutation, dict)
            or not isinstance(mutants, list)
            or len(mutants) != 3
            or mutation.get("verdict") != "CAUGHT"
            or not isinstance(old_mutant, dict)
            or old_mutant.get("id")
            != feature_contract["mutations"][0]["id"]
            or old_mutant.get("expected_failed_property")
            != "directional_layering"
            or old_mutant.get("observed_failed_properties")
            != ["directional_layering"]
            or old_mutant.get("neutral_identity") != "PASS"
            or old_mutant.get("directional_layering") != "FAIL"
            or not isinstance(mutation_diffuse, dict)
            or mutation_diffuse.get("name") != "diffuse"
            or mutation_diffuse.get("population") != width * height
            or mutation_diffuse.get("changed_pixels") != width * height
            or not isinstance(mutation_diffuse_signed, dict)
            or not number(mutation_diffuse_signed.get("max"))
            or mutation_diffuse_signed["max"] >= 0
            or old_mutant.get("expected_full_wet_scale") != 0.5
            or not number(old_mutant.get("maximum_scale_residual"))
            or old_mutant["maximum_scale_residual"] > 1.0e-5
            or not isinstance(mutation_specular, dict)
            or mutation_specular.get("name") != "specular"
            or mutation_specular.get("changed_pixels", 0) <= 0
            or mutation_specular.get("delta_energy", 0) <= 0
            or old_mutant.get("verdict") != "CAUGHT"
            or not isinstance(pi_mutant, dict)
            or pi_mutant.get("id")
            != feature_contract["mutations"][1]["id"]
            or pi_mutant.get("expected_failed_property") != "wet_lobe_scale"
            or pi_mutant.get("neutral_identity") != "PASS"
            or pi_mutant.get("wet_lobe_scale") != "FAIL"
            or pi_mutant.get("expected_ratio") != 0.65
            or not number(pi_mutant.get("maximum_absolute_residual"))
            or pi_mutant["maximum_absolute_residual"] > 1.0e-5
            or not number(pi_mutant.get("maximum_relative_residual"))
            or pi_mutant["maximum_relative_residual"] > 1.0e-4
            or pi_mutant.get("verdict") != "CAUGHT"
            or not isinstance(ambient_mutant, dict)
            or ambient_mutant.get("id")
            != feature_contract["mutations"][2]["id"]
            or ambient_mutant.get("expected_failed_property")
            != "ambient_ibl_layering"
            or ambient_mutant.get("neutral_identity") != "PASS"
            or ambient_mutant.get("ambient_ibl_layering") != "FAIL"
            or not number(
                ambient_mutant.get("maximum_untouched_residual")
            )
            or ambient_mutant["maximum_untouched_residual"] > 1.0e-5
            or ambient_mutant.get("verdict") != "CAUGHT"
        ):
            fail("property:mutation_sensitivity")

    buckets = measurement.get("cross_buckets")
    expected_keys = [
        (mask, material, ibl)
        for material in feature_contract["material_buckets"]
        for ibl in ("inactive", "active")
        for mask in feature_contract["mask_buckets"]
    ]
    observed_keys: list[tuple[str, str, str]] = []
    bucket_by_key: dict[tuple[str, str, str], dict[str, Any]] = {}
    populations: dict[str, set[int]] = {
        name: set() for name in feature_contract["mask_buckets"]
    }
    ranges: dict[str, set[tuple[float, float]]] = {
        name: set() for name in feature_contract["mask_buckets"]
    }
    if not isinstance(buckets, list):
        fail("cross_buckets")
    else:
        for bucket in buckets:
            if not isinstance(bucket, dict):
                fail("cross_bucket")
                continue
            key = (
                str(bucket.get("mask")),
                str(bucket.get("material")),
                str(bucket.get("ibl")),
            )
            observed_keys.append(key)
            bucket_by_key[key] = bucket
            population = bucket.get("population")
            if (
                bucket.get("fixture") != fixture
                or not isinstance(population, int)
                or isinstance(population, bool)
                or population < feature_contract["minimum_bucket_population"]
                or population > width * height
            ):
                fail("cross_bucket_population")
                population = 0
            expected_evidence = expected_bucket_evidence.get(key[0])
            if (
                expected_evidence is None
                or population != expected_evidence["population"]
            ):
                fail("cross_bucket_expected_population")
            if key[0] in populations and population:
                populations[key[0]].add(population)
            post_upload = bucket.get("post_upload")
            if (
                not isinstance(post_upload, dict)
                or not number(post_upload.get("minimum"))
                or not number(post_upload.get("maximum"))
                or post_upload["minimum"] > post_upload["maximum"]
            ):
                fail("post_upload")
            else:
                minimum = float(post_upload["minimum"])
                maximum = float(post_upload["maximum"])
                if (
                    (key[0] == "mask.zero" and (minimum != 0.0 or maximum != 0.0))
                    or (key[0] == "mask.full" and (minimum != 1.0 or maximum != 1.0))
                    or (
                        key[0] == "mask.partial"
                        and not (0.0 < minimum <= maximum < 1.0)
                    )
                ):
                    fail("post_upload_range")
                if (
                    expected_evidence is None
                    or not close(
                        minimum, expected_evidence["minimum"], 1.0e-7
                    )
                    or not close(
                        maximum, expected_evidence["maximum"], 1.0e-7
                    )
                ):
                    fail("post_upload_expected_range")
                if key[0] in ranges:
                    ranges[key[0]].add((minimum, maximum))
            mrt = bucket.get("mrt")
            if (
                not isinstance(mrt, list)
                or len(mrt) != len(feature_contract["mrt_buckets"])
                or any(not isinstance(item, dict) for item in mrt)
                or [item.get("name") for item in mrt if isinstance(item, dict)]
                != feature_contract["mrt_buckets"]
            ):
                fail("mrt")
                continue
            for stats in mrt:
                distribution = stats.get("absolute_delta")
                stats_population = stats.get("population")
                changed_pixels = stats.get("changed_pixels")
                changed_channels = stats.get("changed_channels")
                stock_energy = stats.get("stock_energy")
                delta_energy = stats.get("delta_energy")
                threshold = stats.get("denominator_threshold")
                fraction = stats.get("delta_fraction_of_stock")
                if (
                    any(
                        not number(value) or value < 0
                        for value in (
                            stats_population,
                            changed_pixels,
                            changed_channels,
                            stock_energy,
                            delta_energy,
                            threshold,
                            fraction,
                        )
                    )
                    or not isinstance(stats_population, int)
                    or not isinstance(changed_pixels, int)
                    or not isinstance(changed_channels, int)
                    or stats_population != population
                    or changed_pixels > population
                    or changed_channels > population * 3
                    or stats.get("energy_verdict") != "PROVEN"
                    or not isinstance(distribution, dict)
                    or any(
                        not number(distribution.get(name))
                        or distribution[name] < 0
                        for name in ("min", "mean", "p50", "p95", "p99", "max")
                    )
                ):
                    fail("mrt_stats")
                    continue
                ordered_distribution = [
                    distribution["min"],
                    distribution["p50"],
                    distribution["p95"],
                    distribution["p99"],
                    distribution["max"],
                ]
                if (
                    ordered_distribution != sorted(ordered_distribution)
                    or not (
                        distribution["min"]
                        <= distribution["mean"]
                        <= distribution["max"]
                    )
                ):
                    fail("mrt_distribution_order")
                channel_count = population * 3
                if (
                    (
                        distribution["min"] > 0
                        and (
                            changed_channels != channel_count
                            or changed_pixels != population
                        )
                    )
                    or (
                        changed_channels < channel_count
                        and distribution["min"] != 0
                    )
                    or (
                        changed_channels > 0
                        and distribution["max"] <= 0
                    )
                ):
                    fail("mrt_distribution_changes")
                if not close(
                    distribution["mean"] * channel_count,
                    delta_energy,
                    1.0e-8,
                ):
                    fail("mrt_mean_energy")
                expected_threshold = max(1.0e-12, population * 3.0e-12)
                if (
                    stock_energy <= threshold
                    or not close(threshold, expected_threshold, 1.0e-12)
                    or not close(
                        fraction,
                        delta_energy / stock_energy,
                        1.0e-8,
                    )
                ):
                    fail("mrt_energy_fraction")
                zero_delta_expected = key[0] == "mask.zero"
                if zero_delta_expected:
                    if (
                        changed_pixels != 0
                        or changed_channels != 0
                        or delta_energy != 0
                        or fraction != 0
                        or any(value != 0 for value in distribution.values())
                    ):
                        fail("mrt_expected_zero")
                elif stats["name"] == "diffuse":
                    if (
                        changed_pixels != population
                        or changed_channels < population
                        or delta_energy <= 0
                    ):
                        fail("mrt_diffuse_locality")
                elif key[1] == "material.default":
                    if (
                        changed_pixels < 1
                        or changed_channels < 1
                        or delta_energy <= 0
                    ):
                        fail("mrt_default_specular_locality")
                if (
                    (changed_channels == 0) != (delta_energy == 0)
                    or (changed_pixels == 0) != (changed_channels == 0)
                ):
                    fail("mrt_change_consistency")
        if observed_keys != expected_keys or len(buckets) != len(expected_keys):
            fail("cross_bucket_matrix")
        for mask in feature_contract["mask_buckets"]:
            if len(populations[mask]) != 1 or len(ranges[mask]) != 1:
                fail(f"cross_bucket_consistency:{mask}")
        if all(len(populations[mask]) == 1 for mask in populations):
            if sum(next(iter(populations[mask])) for mask in populations) != width * height:
                fail("cross_bucket_population_total")
        if isinstance(monotonicity, dict):
            diffuse_series = monotonicity.get("diffuse_series")
            if isinstance(diffuse_series, list):
                for series in diffuse_series:
                    if not isinstance(series, dict):
                        continue
                    levels = series.get("levels")
                    full_bucket = bucket_by_key.get(
                        (
                            "mask.full",
                            str(series.get("material")),
                            str(series.get("ibl")),
                        )
                    )
                    if (
                        not isinstance(levels, list)
                        or not levels
                        or not isinstance(levels[-1], dict)
                        or full_bucket is None
                    ):
                        continue
                    full_mrt = full_bucket.get("mrt")
                    if (
                        not isinstance(full_mrt, list)
                        or not full_mrt
                        or not isinstance(full_mrt[0], dict)
                    ):
                        continue
                    full_energy = full_mrt[0].get("delta_energy")
                    final_energy = levels[-1].get("diffuse_delta_energy")
                    if (
                        not number(full_energy)
                        or not number(final_energy)
                        or final_energy <= 0
                        or final_energy + 1.0e-8 < full_energy
                    ):
                        fail("monotonicity:diffuse_cross_evidence")
            probe = monotonicity.get("ibl_specular_probe")
            if isinstance(probe, dict):
                probe_levels = probe.get("levels")
                if (
                    not isinstance(probe_levels, list)
                    or not probe_levels
                    or not isinstance(probe_levels[-1], dict)
                    or not number(probe_levels[-1].get("delta_energy"))
                    or probe_levels[-1]["delta_energy"] <= 0
                ):
                    fail("monotonicity:ibl_specular_cross_evidence")

    if measurement.get("verdict") != "PASS":
        fail("verdict")
    if _has_absolute_path(measurement):
        fail("absolute_path")
    return failures


def _regular_tolerance_failure(
    measurement: dict[str, Any],
    measurement_failures: list[dict[str, str]],
    target: str,
    fixture: str,
) -> OrderedDict[str, str] | None:
    if measurement_failures:
        return None
    if int(measurement.get("aggregate", {}).get("divergent_pixels", 0)) == 0:
        return None
    return OrderedDict(
        (("code", "tolerance_breach"), ("detail", f"{target}:{fixture}"))
    )


def classify_result(
    complete_fixtures: bool,
    mutations_required: bool,
    failures: list[dict[str, str]],
    aggregate: dict[str, Any],
    mutations: list[dict[str, Any]],
) -> tuple[str, int]:
    failure_codes = {item.get("code") for item in failures}
    if "harness_runtime" in failure_codes:
        return "UNPROVEN", 3
    if not complete_fixtures or not mutations_required:
        return "UNPROVEN", 2
    measured_failure_codes = {"tolerance_breach"}
    infrastructure_failures = [
        item for item in failures if item.get("code") not in measured_failure_codes
    ]
    if infrastructure_failures or any(
        item.get("verdict") == "UNPROVEN" for item in mutations
    ):
        return "UNPROVEN", 2
    if (
        failure_codes & measured_failure_codes
        or int(aggregate.get("divergent_pixels", 0)) > 0
    ):
        return "FAIL", 1
    if any(item.get("verdict") == "MISSED" for item in mutations):
        return "FAIL", 1
    return "PASS", 0


def check_report_bytes(path: str, current: bytes) -> bool:
    try:
        stored = Path(path).read_bytes()
        parsed = json.loads(stored.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return False
    return stored == _canonical_bytes(parsed) == current


def _run_harness(
    executable: str,
    reference: str,
    candidate: str,
    fixture: str,
    profile: str,
    required_buckets: list[str],
    minimum_population: int,
    seeds: int,
    seed_base: int,
    width: int,
    height: int,
    tolerance_absolute: float,
    tolerance_relative: float,
    temporary_directory: str,
    label: str,
    verbose: bool,
) -> tuple[dict[str, Any] | None, int, str]:
    measurement_path = os.path.join(temporary_directory, f"{label}.json")
    command = [
        executable,
        reference,
        candidate,
        "--fixture",
        fixture,
        "--seeds",
        str(seeds),
        "--seed-base",
        str(seed_base),
        "--width",
        str(width),
        "--height",
        str(height),
        "--tol-abs",
        format(tolerance_absolute, ".9g"),
        "--tol-rel",
        format(tolerance_relative, ".9g"),
        "--minimum-bucket-population",
        str(minimum_population),
        "--measurement-json",
        measurement_path,
    ]
    for bucket in required_buckets:
        command += ["--required-bucket", bucket]
    if verbose:
        command.append("--verbose")
    process = subprocess.run(command, capture_output=True, text=True)
    human = (process.stdout or "") + (process.stderr or "")
    if not os.path.isfile(measurement_path):
        return None, process.returncode, human
    try:
        measurement = _load_json(measurement_path, "measurement_protocol")
    except StableFailure:
        return None, process.returncode, human
    return measurement, process.returncode, human


def _run_feature_harness(
    executable: str,
    artifacts: dict[str, dict[str, str]],
    fixture: str,
    minimum_population: int,
    seed_base: int,
    width: int,
    height: int,
    temporary_directory: str,
    verbose: bool,
) -> tuple[dict[str, Any] | None, int, str]:
    measurement_path = os.path.join(
        temporary_directory, f"wetness-{fixture}.json"
    )
    command = [
        executable,
        artifacts["directional"]["stock"],
        artifacts["directional"]["feature"],
        "--wetness-feature",
        "--stock-ibl",
        artifacts["directional-ibl"]["stock"],
        "--feature-ibl",
        artifacts["directional-ibl"]["feature"],
        "--mutant",
        artifacts["directional"]["mutant"],
        "--mutant-ibl",
        artifacts["directional-ibl"]["mutant"],
        "--pi-derating-mutant",
        artifacts["directional"]["mutants"][
            "directional-wetness-historical-pi-065-derating"
        ],
        "--ambient-untouched-mutant-ibl",
        artifacts["directional-ibl"]["mutants"][
            "directional-wetness-ambient-ibl-untouched"
        ],
        "--fixture",
        fixture,
        "--seed-base",
        str(seed_base),
        "--width",
        str(width),
        "--height",
        str(height),
        "--minimum-bucket-population",
        str(minimum_population),
        "--measurement-json",
        measurement_path,
    ]
    if verbose:
        command.append("--verbose")
    process = subprocess.run(command, capture_output=True, text=True)
    human = (process.stdout or "") + (process.stderr or "")
    if not os.path.isfile(measurement_path):
        return None, process.returncode, human
    try:
        measurement = _load_json(
            measurement_path, "feature_measurement_protocol"
        )
    except StableFailure:
        return None, process.returncode, human
    return measurement, process.returncode, human


def _run_ambient_feature_harness(
    executable: str,
    artifacts: dict[str, str],
    fixture: str,
    minimum_population: int,
    seed_base: int,
    width: int,
    height: int,
    temporary_directory: str,
    verbose: bool,
) -> tuple[dict[str, Any] | None, int, str]:
    measurement_path = os.path.join(
        temporary_directory, f"wetness-ambient-{fixture}.json"
    )
    command = [
        executable,
        artifacts["stock"],
        artifacts["feature"],
        "--wetness-feature",
        "--wetness-target",
        "ambient-runtime",
        "--mutant",
        artifacts["mutant"],
        "--fixture",
        fixture,
        "--seed-base",
        str(seed_base),
        "--width",
        str(width),
        "--height",
        str(height),
        "--minimum-bucket-population",
        str(minimum_population),
        "--measurement-json",
        measurement_path,
    ]
    if verbose:
        command.append("--verbose")
    process = subprocess.run(command, capture_output=True, text=True)
    human = (process.stdout or "") + (process.stderr or "")
    if not os.path.isfile(measurement_path):
        return None, process.returncode, human
    try:
        measurement = _load_json(
            measurement_path, "feature_measurement_protocol"
        )
    except StableFailure:
        return None, process.returncode, human
    return measurement, process.returncode, human


def _entry_maps(
    manifest: dict[str, Any], contracts: dict[str, Any]
) -> tuple[dict[str, dict[str, Any]], list[dict[str, Any]]]:
    manifest_entries = {
        entry["name"]: entry
        for entry in manifest.get("shaders", [])
        if isinstance(entry, dict) and entry.get("name")
    }
    selected: list[dict[str, Any]] = []
    for contract in contracts["contracts"]:
        entry = manifest_entries.get(contract["name"])
        if entry is None:
            raise StableFailure("manifest_contract", contract["name"])
        selected.append(
            {
                **contract,
                "src": entry["src"],
                "defines": list(entry.get("defines", [])),
                "corpus_sha1": entry.get("corpus_sha1"),
            }
        )
    return manifest_entries, selected


def _report_scope(selected_targets: set[str], all_targets: set[str]) -> str:
    return "full" if selected_targets == all_targets else "focused"


def _validate_scope_artifacts(
    scope: str,
    manifest_directory: str | None,
    check_report: str | None,
) -> None:
    if scope == "focused" and (manifest_directory or check_report):
        raise StableFailure(
            "focused_scope_artifact",
            "focused runs cannot publish or check the canonical manifest",
        )


def _relevant_mutations(
    mutations: list[dict[str, Any]], selected_targets: set[str]
) -> list[dict[str, Any]]:
    return [
        mutation
        for mutation in mutations
        if mutation["target"] in selected_targets
    ]


def _fixture_result(
    target: str,
    profile: str,
    ibl_in_light: bool,
    fixture: str,
    measurement: dict[str, Any],
) -> OrderedDict[str, Any]:
    return OrderedDict(
        (
            ("target", target),
            ("profile", profile),
            ("ibl_in_light", ibl_in_light),
            ("fixture", fixture),
            ("harness_source_sha256", measurement.get("source_sha256", "")),
            ("width", measurement.get("width", 0)),
            ("height", measurement.get("height", 0)),
            ("execution_environment", measurement.get("execution_environment", {})),
            ("generated_inputs_sha256", measurement.get("generated_inputs_sha256", "")),
            ("seed_base", measurement.get("seed_base", 0)),
            ("seeds", measurement.get("seeds", [])),
            ("scenario_seeds", measurement.get("scenario_seeds", [])),
            ("formats", measurement.get("formats", [])),
            ("measurement_format", measurement.get("measurement_format", "")),
            ("buckets", measurement.get("buckets", [])),
            ("aggregate", measurement.get("aggregate", _measurement_metrics_zero())),
            ("verdict", measurement.get("verdict", "UNPROVEN")),
        )
    )


def _mutation_result(
    mutation: dict[str, Any],
    control: dict[str, Any] | None,
    mutant: dict[str, Any] | None,
    minimum_population: int,
    infrastructure_failure: str | None = None,
    artifacts: OrderedDict[str, Any] | None = None,
) -> OrderedDict[str, Any]:
    expected = mutation["expected_bucket"]
    population = 0
    divergent_pixels = 0
    other_depth_divergent_pixels = 0
    control_divergent = -1
    if control:
        control_divergent = int(control.get("aggregate", {}).get("divergent_pixels", -1))
    if mutant:
        bucket = next(
            (item for item in mutant.get("buckets", []) if item.get("name") == expected),
            None,
        )
        if bucket:
            population = int(bucket.get("population", 0))
            divergent_pixels = int(bucket.get("divergent_pixels", 0))
        if mutation["class"] == "boundary-operator":
            other_depth_divergent_pixels = sum(
                int(item.get("divergent_pixels", 0))
                for item in mutant.get("buckets", [])
                if item.get("name") in {"depth.near", "depth.far"}
            )
    caught = (
        infrastructure_failure is None
        and control is not None
        and mutant is not None
        and control_divergent == 0
        and population >= minimum_population
        and divergent_pixels > 0
        and other_depth_divergent_pixels == 0
    )
    verdict = (
        "UNPROVEN"
        if infrastructure_failure is not None
        else ("CAUGHT" if caught else "MISSED")
    )
    return OrderedDict(
        (
            ("id", mutation["id"]),
            ("class", mutation["class"]),
            ("target", mutation["target"]),
            ("fixture", mutation["fixture"]),
            ("expected_bucket", expected),
            ("infrastructure_failure", infrastructure_failure),
            ("artifacts", artifacts or OrderedDict()),
            ("expected_bucket_population", population),
            ("expected_bucket_divergent_pixels", divergent_pixels),
            (
                "other_depth_bucket_divergent_pixels",
                other_depth_divergent_pixels,
            ),
            ("control_divergent_pixels", control_divergent),
            ("verdict", verdict),
        )
    )


def _print_human(label: str, measurement: dict[str, Any] | None, output: str) -> None:
    if measurement:
        aggregate = measurement.get("aggregate", {})
        print(
            f"  {label}: {measurement.get('verdict', 'UNPROVEN')} "
            f"({aggregate.get('divergent_pixels', 0)} divergent pixels)"
        )
    else:
        print(f"  {label}: ERROR")
    if output.strip():
        for line in output.rstrip().splitlines():
            print(f"    {line}", file=sys.stderr)


def _make_report(
    schema: str,
    scope: str,
    authoritative: bool,
    producer: OrderedDict[str, Any],
    compiler: OrderedDict[str, Any],
    configuration: OrderedDict[str, Any],
    inputs: list[OrderedDict[str, Any]],
    fixtures: list[OrderedDict[str, Any]],
    mutations: list[OrderedDict[str, Any]],
    aggregate: OrderedDict[str, Any],
    failures: list[OrderedDict[str, str]],
    verdict: str,
) -> OrderedDict[str, Any]:
    return OrderedDict(
        (
            ("schema", schema),
            ("schema_version", 1),
            ("scope", scope),
            ("authoritative", authoritative),
            ("producer", producer),
            ("compiler", compiler),
            ("configuration", configuration),
            ("inputs", inputs),
            ("fixtures", fixtures),
            ("mutations", mutations),
            ("aggregate", aggregate),
            ("failures", failures),
            ("verdict", verdict),
        )
    )


def _feature_compiler_flags(
    source_label: str, defines: list[str], output_label: str
) -> list[str]:
    return [
        *COMPILE_FLAGS,
        "/I",
        "original-source-directory",
        *[part for define in defines for part in ("/D", define)],
        "/Fo",
        output_label,
        source_label,
    ]


def _feature_property_summary(
    measurements: list[dict[str, Any]],
    property_name: str,
    target: str | None = None,
) -> OrderedDict[str, Any]:
    fixture_verdicts: list[OrderedDict[str, Any]] = []
    for measurement in measurements:
        if target is not None and measurement.get("target") != target:
            continue
        properties = measurement.get("properties")
        property_value = (
            properties.get(property_name)
            if isinstance(properties, dict)
            else None
        )
        property_verdict = (
            property_value.get("verdict")
            if isinstance(property_value, dict)
            else "UNPROVEN"
        )
        if property_verdict not in {"PASS", "FAIL", "UNPROVEN"}:
            property_verdict = "UNPROVEN"
        fixture_verdicts.append(
            OrderedDict(
                (
                    ("target", str(measurement.get("target", "unknown"))),
                    ("fixture", str(measurement.get("fixture", "unknown"))),
                    ("verdict", property_verdict),
                )
            )
        )
    verdict = "PASS"
    if any(item["verdict"] == "UNPROVEN" for item in fixture_verdicts):
        verdict = "UNPROVEN"
    elif any(item["verdict"] == "FAIL" for item in fixture_verdicts):
        verdict = "FAIL"
    return OrderedDict((("fixtures", fixture_verdicts), ("verdict", verdict)))


def run_additive_feature(
    args: argparse.Namespace,
) -> tuple[OrderedDict[str, Any], int]:
    if args.additive_feature != "wetness-effects":
        raise StableFailure("feature_selection", "unknown additive feature")
    if args.fixture != "all":
        raise StableFailure(
            "feature_fixture_incomplete",
            "WetnessEffects evidence requires adversarial and native fixtures",
        )
    if args.mutation_suite != "required":
        raise StableFailure(
            "feature_mutation_incomplete",
            "WetnessEffects evidence requires the feature mutation",
        )
    if args.shader:
        raise StableFailure(
            "feature_selection", "--shader is not valid for additive-feature mode"
        )
    if not os.path.isfile(args.exe):
        raise StableFailure("harness_missing", "execution harness is missing")

    contracts_capture = _capture_file(
        args.contracts, "scripts/shaders/shader-exec-contracts.json"
    )
    try:
        contracts = json.loads(contracts_capture["data"].decode("utf-8-sig"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise StableFailure("contracts_read", error.__class__.__name__) from error
    validate_contracts(contracts)
    validate_additive_features(contracts.get("additive_features"))
    feature = contracts["additive_features"]["wetness-effects"]
    if (
        args.width < feature["dimensions"]["minimum_width"]
        or args.height < feature["dimensions"]["minimum_height"]
    ):
        raise StableFailure(
            "feature_dimensions",
            "WetnessEffects dimensions are below the contract minimum",
        )

    fxc = _find_fxc(args.fxc)
    python_capture = _capture_file(__file__, "scripts/shaders/shader_exec_diff.py")
    harness_source_capture = _capture_file(
        HARNESS_SOURCE, "scripts/shaders/shader_exec_diff/main.cpp"
    )
    harness_executable_capture = _capture_file(
        args.exe, "shader_exec_diff.exe"
    )
    compiler_capture = _capture_file(fxc, "fxc.exe")
    compiler_version = _windows_file_version(fxc)
    if re.fullmatch(r"\d+\.\d+\.\d+\.\d+", compiler_version) is None:
        raise StableFailure("compiler_identity", "fxc version is unavailable")
    compiler_banner = _compiler_banner(fxc)
    target_contracts = OrderedDict(
        (
            (
                "directional",
                _feature_target_contract(feature, "directional"),
            ),
            (
                "ambient-runtime",
                _feature_target_contract(feature, "ambient-runtime"),
            ),
        )
    )
    source_closures: dict[str, OrderedDict[str, Any]] = {}
    source_captures_by_target: dict[str, list[dict[str, Any]]] = {}
    mutant_bytes_by_target: dict[str, OrderedDict[str, bytes]] = {}
    for target, target_contract in target_contracts.items():
        source_label = target_contract["source"]
        source_path = os.path.join(
            REPO_ROOT, source_label.replace("/", os.sep)
        )
        source_closure, source_captures = _capture_hlsl_closure(source_path)
        source_capture = next(
            (
                capture
                for capture in source_captures
                if capture["label"] == source_label
            ),
            None,
        )
        if source_capture is None:
            raise StableFailure("feature_source_closure", source_label)
        source_closures[target] = source_closure
        source_captures_by_target[target] = source_captures
        original = source_capture["data"].decode("utf-8-sig")
        mutant_bytes_by_target[target] = OrderedDict(
            (
                mutation["id"],
                build_additive_feature_mutant(
                    original, target_contract, mutation
                ).encode("utf-8"),
            )
            for mutation in _feature_mutations(target_contract)
        )

    live_captures = [
        contracts_capture,
        python_capture,
        harness_source_capture,
        harness_executable_capture,
        compiler_capture,
        *[
            capture
            for captures in source_captures_by_target.values()
            for capture in captures
        ],
    ]
    harness_source = harness_source_capture["artifact"]
    harness_self_test = _run_harness_self_test(
        args.exe, harness_source["sha256"]
    )
    artifact_paths: dict[str, dict[str, dict[str, str]]] = {}
    variant_records: list[OrderedDict[str, Any]] = []
    measurements: list[dict[str, Any]] = []
    failures: list[OrderedDict[str, str]] = []
    generated_captures: list[dict[str, Any]] = []

    with tempfile.TemporaryDirectory(
        prefix="fo4cs-wetness-feature-"
    ) as temporary:
        snapshot_sources: dict[str, str] = {}
        mutant_sources: dict[str, OrderedDict[str, str]] = {}
        mutation_labels: dict[str, OrderedDict[str, str]] = {}
        for target, target_contract in target_contracts.items():
            source_label = target_contract["source"]
            source_captures = source_captures_by_target[target]
            snapshot_root = os.path.join(
                temporary, "source-snapshot", target
            )
            snapshot_sources[target] = _write_hlsl_snapshot(
                snapshot_root, source_captures, source_label
            )
            for capture in source_captures:
                snapshot_path = os.path.join(
                    snapshot_root, *str(capture["label"]).split("/")
                )
                snapshot_capture = _capture_file(
                    snapshot_path,
                    f"snapshot/{target}/{capture['label']}",
                )
                if snapshot_capture["data"] != capture["data"]:
                    raise StableFailure(
                        "feature_source_snapshot",
                        f"{target}:{capture['label']}",
                    )
                generated_captures.append(snapshot_capture)
            mutant_sources[target] = OrderedDict()
            mutation_labels[target] = OrderedDict()
            for mutation in _feature_mutations(target_contract):
                mutation_id = mutation["id"]
                mutation_label = f"generated/{target}/{mutation_id}.hlsl"
                mutant_source = os.path.join(
                    temporary, *mutation_label.split("/")
                )
                Path(mutant_source).parent.mkdir(
                    parents=True, exist_ok=True
                )
                Path(mutant_source).write_bytes(
                    mutant_bytes_by_target[target][mutation_id]
                )
                generated_captures.append(
                    _capture_file(mutant_source, mutation_label)
                )
                mutant_sources[target][mutation_id] = mutant_source
                mutation_labels[target][mutation_id] = mutation_label

        for target, target_contract in target_contracts.items():
            source_label = target_contract["source"]
            snapshot_source = snapshot_sources[target]
            artifact_paths[target] = {}
            for variant in target_contract["variants"]:
                variant_id = variant["id"]
                stock_defines = list(variant["defines"])
                feature_defines = sorted(
                    [*stock_defines, "WETNESS_EFFECTS=1"]
                )
                artifact_paths[target][variant_id] = {"mutants": {}}
                artifact_records: dict[
                    str, tuple[list[str], str, OrderedDict[str, Any]]
                ] = {}
                for kind, compile_source, defines in (
                    ("stock", snapshot_source, stock_defines),
                    ("feature", snapshot_source, feature_defines),
                ):
                    output_name = (
                        f"{target}-{variant_id}-{kind}.dxbc"
                    )
                    output = os.path.join(temporary, output_name)
                    compiled, log = _compile_shader(
                        fxc,
                        compile_source,
                        os.path.dirname(snapshot_source),
                        defines,
                        output,
                    )
                    if not compiled:
                        if log.strip():
                            print(log.rstrip(), file=sys.stderr)
                        raise StableFailure(
                            "feature_compile_failed",
                            f"{target}:{variant_id}:{kind}",
                        )
                    artifact_paths[target][variant_id][kind] = output
                    artifact_capture = _capture_file(
                        output, f"generated/{output_name}"
                    )
                    generated_captures.append(artifact_capture)
                    artifact_records[kind] = (
                        defines,
                        output_name,
                        artifact_capture["artifact"],
                    )
                mutant_records: list[OrderedDict[str, Any]] = []
                for mutation in _feature_mutations(target_contract):
                    mutation_variants = mutation.get("variants")
                    if (
                        isinstance(mutation_variants, list)
                        and variant_id not in mutation_variants
                    ):
                        continue
                    mutation_id = mutation["id"]
                    output_name = (
                        f"{target}-{variant_id}-mutant-{mutation_id}.dxbc"
                    )
                    output = os.path.join(temporary, output_name)
                    compiled, log = _compile_shader(
                        fxc,
                        mutant_sources[target][mutation_id],
                        os.path.dirname(snapshot_source),
                        feature_defines,
                        output,
                    )
                    if not compiled:
                        if log.strip():
                            print(log.rstrip(), file=sys.stderr)
                        raise StableFailure(
                            "feature_compile_failed",
                            f"{target}:{variant_id}:{mutation_id}",
                        )
                    artifact_paths[target][variant_id]["mutants"][
                        mutation_id
                    ] = output
                    if mutation_id == target_contract["mutation"]["id"]:
                        artifact_paths[target][variant_id]["mutant"] = output
                    artifact_capture = _capture_file(
                        output, f"generated/{output_name}"
                    )
                    generated_captures.append(artifact_capture)
                    mutant_records.append(
                        OrderedDict(
                            (
                                ("id", mutation_id),
                                ("defines", feature_defines),
                                (
                                    "compiler_flags",
                                    _feature_compiler_flags(
                                        mutation_labels[target][mutation_id],
                                        feature_defines,
                                        output_name,
                                    ),
                                ),
                                ("dxbc", artifact_capture["artifact"]),
                            )
                        )
                    )
                record_items: list[tuple[str, Any]] = [
                    ("target", target),
                    ("id", variant_id),
                ]
                if "ibl" in variant:
                    record_items.append(("ibl", variant["ibl"]))
                for kind in ("stock", "feature"):
                    defines, output_name, artifact = artifact_records[kind]
                    record_items.append(
                        (
                            kind,
                            OrderedDict(
                                (
                                    ("defines", defines),
                                    (
                                        "compiler_flags",
                                        _feature_compiler_flags(
                                            source_label,
                                            defines,
                                            output_name,
                                        ),
                                    ),
                                    ("dxbc", artifact),
                                )
                            ),
                        )
                    )
                record_items.append(("mutants", mutant_records))
                variant_records.append(OrderedDict(record_items))

        for target in target_contracts:
            for fixture in FIXTURE_ORDER:
                if target == "directional":
                    measurement, return_code, human = _run_feature_harness(
                        args.exe,
                        artifact_paths[target],
                        fixture,
                        feature["minimum_bucket_population"],
                        args.seed_base,
                        args.width,
                        args.height,
                        temporary,
                        args.verbose,
                    )
                else:
                    measurement, return_code, human = (
                        _run_ambient_feature_harness(
                            args.exe,
                            artifact_paths[target]["ambient-runtime"],
                            fixture,
                            feature["minimum_bucket_population"],
                            args.seed_base,
                            args.width,
                            args.height,
                            temporary,
                            args.verbose,
                        )
                    )
                if args.verbose and human.strip():
                    for line in human.rstrip().splitlines():
                        print(f"  {line}")
                detail = f"{target}:{fixture}"
                if measurement is None:
                    if human.strip():
                        print(human.rstrip(), file=sys.stderr)
                    raise StableFailure(
                        "feature_harness_runtime",
                        f"{detail}: no measurement",
                    )
                validation = validate_feature_measurement(
                    measurement,
                    fixture,
                    feature,
                    args.width,
                    args.height,
                    harness_source["sha256"],
                )
                failures.extend(validation)
                if return_code == 1:
                    failures.append(
                        _feature_failure_record(
                            "feature_property_failure", detail
                        )
                    )
                elif return_code != 0:
                    failures.append(
                        _feature_failure_record(
                            "feature_harness_runtime", detail
                        )
                    )
                measurements.append(measurement)
                print(
                    f"== WetnessEffects {target} {fixture}: "
                    f"{measurement.get('verdict', 'UNPROVEN')} =="
                )

        stale_inputs = _verify_captured_files(
            [*live_captures, *generated_captures]
        )
        for label in stale_inputs:
            failures.append(
                _feature_failure_record("stale_input", label)
            )
        variant_records.sort(
            key=lambda item: (item["target"], item["id"])
        )
        target_order = {"directional": 0, "ambient-runtime": 1}
        fixture_order = {
            fixture: index for index, fixture in enumerate(FIXTURE_ORDER)
        }
        measurements.sort(
            key=lambda item: (
                target_order.get(str(item.get("target")), 99),
                fixture_order.get(str(item.get("fixture")), 99),
            )
        )
        failures = sorted(
            {
                json.dumps(item, sort_keys=True): item
                for item in failures
            }.values(),
            key=lambda item: (item["code"], item["detail"]),
        )
        property_names = (
            "neutral_identity",
            "active_locality",
            "magnitude",
            "wet_lobe_scale",
            "ambient_ibl_layering",
            "matte_sheen",
            "no_ibl_film",
            "ambient_film_blend",
            "monotonicity",
        )
        directional_properties = {
            "wet_lobe_scale",
            "ambient_ibl_layering",
        }
        ambient_properties = {
            "matte_sheen",
            "no_ibl_film",
            "ambient_film_blend",
        }
        properties = OrderedDict(
            (
                name,
                _feature_property_summary(
                    measurements,
                    name,
                    (
                        "directional"
                        if name in directional_properties
                        else (
                            "ambient-runtime"
                            if name in ambient_properties
                            else None
                        )
                    ),
                ),
            )
            for name in property_names
        )
        mutation_targets: list[OrderedDict[str, Any]] = []
        for target, target_contract in target_contracts.items():
            for contract_mutation in _feature_mutations(target_contract):
                mutation_fixtures: list[OrderedDict[str, Any]] = []
                for measurement in measurements:
                    if measurement.get("target") != target:
                        continue
                    measurement_properties = measurement.get("properties")
                    sensitivity = (
                        measurement_properties.get(
                            "mutation_sensitivity"
                        )
                        if isinstance(measurement_properties, dict)
                        else None
                    )
                    sensitivity = (
                        sensitivity if isinstance(sensitivity, dict) else {}
                    )
                    if target == "directional":
                        mutation = next(
                            (
                                item
                                for item in sensitivity.get("mutants", [])
                                if isinstance(item, dict)
                                and item.get("id")
                                == contract_mutation["id"]
                            ),
                            {},
                        )
                    else:
                        mutation = sensitivity
                    mutation_fixtures.append(
                        OrderedDict(
                            (
                                (
                                    "fixture",
                                    str(
                                        measurement.get(
                                            "fixture", "unknown"
                                        )
                                    ),
                                ),
                                (
                                    "expected_failed_property",
                                    mutation.get(
                                        "expected_failed_property"
                                    ),
                                ),
                                (
                                    "observed_failed_properties",
                                    mutation.get(
                                        "observed_failed_properties",
                                        [
                                            contract_mutation[
                                                "expected_failed_property"
                                            ]
                                        ]
                                        if mutation.get("verdict")
                                        == "CAUGHT"
                                        else [],
                                    ),
                                ),
                                (
                                    "verdict",
                                    mutation.get(
                                        "verdict", "UNPROVEN"
                                    ),
                                ),
                            )
                        )
                    )
                target_verdict = "CAUGHT"
                if any(
                    item["verdict"] == "UNPROVEN"
                    for item in mutation_fixtures
                ):
                    target_verdict = "UNPROVEN"
                elif any(
                    item["verdict"] != "CAUGHT"
                    for item in mutation_fixtures
                ):
                    target_verdict = "MISSED"
                mutation_targets.append(
                    OrderedDict(
                        (
                            ("target", target),
                            ("id", contract_mutation["id"]),
                            (
                                "expected_failed_property",
                                contract_mutation[
                                    "expected_failed_property"
                                ],
                            ),
                            ("fixtures", mutation_fixtures),
                            ("verdict", target_verdict),
                        )
                    )
                )
        mutation_verdict = (
            "UNPROVEN"
            if any(
                item["verdict"] == "UNPROVEN"
                for item in mutation_targets
            )
            else (
                "CAUGHT"
                if all(
                    item["verdict"] == "CAUGHT"
                    for item in mutation_targets
                )
                else "MISSED"
            )
        )
        properties["mutation_sensitivity"] = OrderedDict(
            (
                ("targets", mutation_targets),
                ("verdict", mutation_verdict),
            )
        )
        property_pass = all(
            properties[name]["verdict"] == "PASS"
            for name in property_names
        ) and properties["mutation_sensitivity"]["verdict"] == "CAUGHT"
        verdict = (
            "STALE"
            if stale_inputs
            else (
                "PASS"
                if property_pass and not failures
                else (
                    "FAIL"
                    if any(
                        item["code"] == "feature_property_failure"
                        for item in failures
                    )
                    else "UNPROVEN"
                )
            )
        )
        capture_verification = OrderedDict(
            (
                (
                    "checked_labels",
                    sorted(
                        str(capture["label"])
                        for capture in [*live_captures, *generated_captures]
                    ),
                ),
                ("changed_labels", stale_inputs),
                ("verdict", "STALE" if stale_inputs else "PASS"),
            )
        )
        report = OrderedDict(
            (
                ("schema", FEATURE_RUN_SCHEMA),
                ("schema_version", 1),
                ("evidence_class", "additive-feature"),
                (
                    "comparison",
                    "reconstructed-stock-vs-reconstructed-feature",
                ),
                ("native_bytecode_used", False),
                (
                    "profile",
                    OrderedDict(
                        (
                            ("name", "wetness-deferred-lighting"),
                            (
                                "claim",
                                "Directional and ambient-runtime WetnessEffects "
                                "additive behavior only; "
                                "not game or native-bytecode parity",
                            ),
                        )
                    ),
                ),
                (
                    "producer",
                    OrderedDict(
                        (
                            ("name", "shader_exec_diff.py"),
                            ("version", TOOL_VERSION),
                            (
                                "python_driver",
                                python_capture["artifact"],
                            ),
                            ("harness_version", HARNESS_VERSION),
                            ("harness_source", harness_source),
                            (
                                "harness_executable",
                                harness_executable_capture["artifact"],
                            ),
                            ("capture_verification", capture_verification),
                        )
                    ),
                ),
                (
                    "measurement_protocol",
                    OrderedDict(
                        (
                            ("name", feature["measurement_protocol"]),
                            ("version", FEATURE_PROTOCOL_VERSION),
                            ("driver_type", "WARP"),
                            ("feature_level", "11_0"),
                            (
                                "comparison",
                                "same reconstructed shader compiled stock "
                                "and with WETNESS_EFFECTS=1",
                            ),
                            ("native_bytecode_used", False),
                            ("width", args.width),
                            ("height", args.height),
                            ("seed_base", args.seed_base),
                            (
                                "runtime_components",
                                harness_self_test["runtime_components"],
                            ),
                        )
                    ),
                ),
                (
                    "contracts",
                    OrderedDict(
                        (
                            (
                                "label",
                                "scripts/shaders/shader-exec-contracts.json",
                            ),
                            (
                                "artifact",
                                contracts_capture["artifact"],
                            ),
                            (
                                "stock_to_feature_delta",
                                "only Texture2D t4 or t13 may be added "
                                "for its declared target",
                            ),
                            (
                                "mutation_exception",
                                "the declared wetness texture may optimize "
                                "away only in mutation mode",
                            ),
                        )
                    ),
                ),
                (
                    "source_closures",
                    [
                        OrderedDict(
                            (
                                ("target", target),
                                ("closure", source_closures[target]),
                            )
                        )
                        for target in target_contracts
                    ],
                ),
                (
                    "mutation_sources",
                    [
                        OrderedDict(
                            (
                                ("target", target),
                                ("id", mutation_id),
                                (
                                    "label",
                                    mutation_labels[target][mutation_id],
                                ),
                                (
                                    "sha256",
                                    hashlib.sha256(
                                        mutant_bytes
                                    ).hexdigest(),
                                ),
                                ("size", len(mutant_bytes)),
                            )
                        )
                        for target in target_contracts
                        for mutation_id, mutant_bytes in
                        mutant_bytes_by_target[target].items()
                    ],
                ),
                (
                    "compiler",
                    OrderedDict(
                        (
                            ("name", "fxc"),
                            ("version", compiler_version),
                            ("banner", compiler_banner),
                            (
                                "binary",
                                compiler_capture["artifact"],
                            ),
                            ("target", "ps_5_0"),
                            ("optimization", "O3"),
                        )
                    ),
                ),
                ("variants", variant_records),
                ("fixtures", measurements),
                ("properties", properties),
                ("failures", failures),
                ("verdict", verdict),
            )
        )
        _assert_receipt_safe(report)
        current = _canonical_bytes(report)
        if args.check_report:
            if not check_report_bytes(args.check_report, current):
                report["verdict"] = "STALE"
                return report, 2
            return report, {"PASS": 0, "FAIL": 1}.get(verdict, 2)
        if args.json:
            _write_canonical(args.json, report)
        if args.manifest_dir:
            _write_canonical(
                os.path.join(
                    args.manifest_dir, FEATURE_MANIFEST_FILENAME
                ),
                report,
            )
        return report, 0 if verdict == "PASS" else (
            1 if verdict == "FAIL" else 2
        )


def _feature_driver_failure_report(
    args: argparse.Namespace,
    code: str,
    detail: str,
) -> OrderedDict[str, Any]:
    safe_detail = code if _has_absolute_path(detail) else detail
    failure = _feature_failure_record(code, safe_detail)
    fixtures = []
    for target, profile in (
        ("directional", "wetness-directional-lighting"),
        ("ambient-runtime", "wetness-ambient-ibl-runtime"),
    ):
        for fixture, wetness_format in (
            ("adversarial", "R32_FLOAT"),
            ("native", "R8_UNORM"),
        ):
            fixtures.append(
                OrderedDict(
                    (
                        ("schema", FEATURE_MEASUREMENT_SCHEMA),
                        ("schema_version", 1),
                        ("harness_version", HARNESS_VERSION),
                        ("evidence_class", "additive-feature"),
                        (
                            "comparison",
                            "reconstructed-stock-vs-reconstructed-feature",
                        ),
                        ("native_bytecode_used", False),
                        ("target", target),
                        ("profile", profile),
                        ("fixture", fixture),
                        ("width", args.width),
                        ("height", args.height),
                        ("measurement_protocol", "wetness-warp-v5"),
                        ("wetness_format", wetness_format),
                        ("failures", [failure]),
                        ("verdict", "UNPROVEN"),
                    )
                )
            )
    report = OrderedDict(
        (
            ("schema", FEATURE_RUN_SCHEMA),
            ("schema_version", 1),
            ("evidence_class", "additive-feature"),
            (
                "comparison",
                "reconstructed-stock-vs-reconstructed-feature",
            ),
            ("native_bytecode_used", False),
            (
                "profile",
                OrderedDict(
                    (
                        ("name", "wetness-deferred-lighting"),
                        (
                            "claim",
                            "Directional and ambient-runtime WetnessEffects "
                            "additive behavior only; "
                            "not game or native-bytecode parity",
                        ),
                    )
                ),
            ),
            ("fixtures", fixtures),
            ("failures", [failure]),
            ("verdict", "UNPROVEN"),
        )
    )
    _assert_receipt_safe(report)
    return report


def _write_feature_failure_artifacts(
    args: argparse.Namespace,
    report: OrderedDict[str, Any],
) -> None:
    if args.json:
        _write_canonical(args.json, report)
    if args.manifest_dir:
        _write_canonical(
            os.path.join(args.manifest_dir, FEATURE_MANIFEST_FILENAME),
            report,
        )


def _parse_arguments(arguments: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="WARP execution diff for reconstructed FO4 deferred shaders."
    )
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--contracts", default=DEFAULT_CONTRACTS)
    parser.add_argument("--corpus-dir", default=DEFAULT_CORPUS_DIR)
    parser.add_argument("--fxc", default=None)
    parser.add_argument("--exe", default=DEFAULT_EXECUTABLE)
    parser.add_argument(
        "--additive-feature", choices=("wetness-effects",), default=None
    )
    parser.add_argument("--shader", action="append")
    parser.add_argument("--fixture", choices=("all", *FIXTURE_ORDER), default="all")
    parser.add_argument(
        "--mutation-suite", choices=("required", "skip"), default="required"
    )
    parser.add_argument("--seeds", type=_positive_int, default=8)
    parser.add_argument("--seed-base", type=_nonnegative_int, default=0)
    parser.add_argument("--width", type=_positive_int, default=256)
    parser.add_argument("--height", type=_positive_int, default=256)
    parser.add_argument("--tol-abs", type=_nonnegative_float, default=2e-3)
    parser.add_argument("--tol-rel", type=_nonnegative_float, default=1e-2)
    parser.add_argument("--json")
    parser.add_argument("--manifest-dir")
    parser.add_argument("--check-report")
    parser.add_argument("-v", "--verbose", action="store_true")
    return parser.parse_args(arguments)


def run(arguments: list[str] | None = None) -> tuple[OrderedDict[str, Any], int]:
    args = _parse_arguments(arguments)
    if args.additive_feature is not None:
        try:
            return run_additive_feature(args)
        except StableFailure as error:
            report = _feature_driver_failure_report(
                args, error.code, error.detail
            )
            _write_feature_failure_artifacts(args, report)
            return report, 2
        except Exception as error:
            report = _feature_driver_failure_report(
                args, "internal_failure", error.__class__.__name__
            )
            _write_feature_failure_artifacts(args, report)
            return report, 3
    if not os.path.isfile(args.exe):
        raise StableFailure("harness_missing", "execution harness is missing")
    fxc = _find_fxc(args.fxc)
    compiler_version = _windows_file_version(fxc)
    if re.fullmatch(r"\d+\.\d+\.\d+\.\d+", compiler_version) is None:
        raise StableFailure("compiler_identity", "fxc version is unavailable")
    compiler_binary = _hash_file(fxc, include_sha1=False)
    compiler_banner = _compiler_banner(fxc)
    contracts = _load_json(args.contracts, "contracts_read")
    validate_contracts(contracts)
    harness_source_hash = _hash_file(HARNESS_SOURCE, include_sha1=False)
    expected_harness_source_sha256 = harness_source_hash["sha256"]
    harness_self_test = _run_harness_self_test(
        args.exe, expected_harness_source_sha256
    )
    runtime_components = harness_self_test["runtime_components"]
    manifest = _load_json(args.manifest, "manifest_read")
    _, entries = _entry_maps(manifest, contracts)
    if args.shader:
        requested = set(args.shader)
        entries = [entry for entry in entries if entry["name"] in requested]
        unknown = requested - {entry["name"] for entry in entries}
        if unknown:
            raise StableFailure("shader_selection", sorted(unknown)[0])
    if not entries:
        raise StableFailure("shader_selection", "no contracts selected")
    all_targets = {entry["name"] for entry in contracts["contracts"]}
    selected_targets = {entry["name"] for entry in entries}
    scope = _report_scope(selected_targets, all_targets)
    _validate_scope_artifacts(scope, args.manifest_dir, args.check_report)

    selected_fixtures = (
        list(FIXTURE_ORDER) if args.fixture == "all" else [args.fixture]
    )
    minimum_population = int(contracts["minimum_bucket_population"])
    failures: list[OrderedDict[str, str]] = []
    fixture_results: list[OrderedDict[str, Any]] = []
    mutation_results: list[OrderedDict[str, Any]] = []
    input_results: list[OrderedDict[str, Any]] = []
    compiled: dict[str, str] = {}
    corpus_paths: dict[str, str] = {}

    with tempfile.TemporaryDirectory(prefix="fo4cs-exec-diff-") as temporary:
        for entry in entries:
            name = entry["name"]
            source = os.path.join(REPO_ROOT, entry["src"])
            corpus = os.path.join(args.corpus_dir, f"{name}.dxbc")
            if not entry.get("corpus_sha1") or not os.path.isfile(corpus):
                failures.append(
                    OrderedDict((("code", "corpus_missing"), ("detail", name)))
                )
                continue
            native_hash = _hash_file(corpus)
            if native_hash["sha1"].lower() != str(entry["corpus_sha1"]).lower():
                failures.append(
                    OrderedDict(
                        (("code", "corpus_hash_mismatch"), ("detail", name))
                    )
                )
                continue
            candidate = os.path.join(temporary, f"{name}.dxbc")
            compiled_ok, compile_log = _compile_shader(
                fxc,
                source,
                os.path.dirname(source),
                entry["defines"],
                candidate,
            )
            if not compiled_ok:
                if compile_log.strip():
                    print(compile_log.rstrip(), file=sys.stderr)
                failures.append(
                    OrderedDict((("code", "compile_failed"), ("detail", name)))
                )
                continue
            compiled[name] = candidate
            corpus_paths[name] = corpus
            source_hash = _hash_file(source, include_sha1=False)
            input_results.append(
                OrderedDict(
                    (
                        ("target", name),
                        ("profile", entry["profile"]),
                        ("ibl_in_light", entry["ibl_in_light"]),
                        ("defines", sorted(entry["defines"])),
                        (
                            "compiler_flags",
                            [
                                *COMPILE_FLAGS,
                                "/I",
                                "original-source-directory",
                                *[
                                    part
                                    for define in entry["defines"]
                                    for part in ("/D", define)
                                ],
                                "/Fo",
                                f"{name}.dxbc",
                                entry["src"].replace("\\", "/"),
                            ],
                        ),
                        ("source", entry["src"].replace("\\", "/")),
                        ("native", native_hash),
                        ("candidate_hlsl", source_hash),
                        ("compiled_dxbc", _hash_file(candidate)),
                    )
                )
            )
            print(f"== {name} ==")
            required_by_fixture = contracts["profiles"][entry["profile"]][
                "required_buckets"
            ]
            for fixture in selected_fixtures:
                measurement, return_code, human = _run_harness(
                    args.exe,
                    corpus,
                    candidate,
                    fixture,
                    entry["profile"],
                    required_by_fixture[fixture],
                    minimum_population,
                    args.seeds,
                    args.seed_base,
                    args.width,
                    args.height,
                    args.tol_abs,
                    args.tol_rel,
                    temporary,
                    f"regular-{name}-{fixture}",
                    args.verbose,
                )
                _print_human(fixture, measurement, human)
                if measurement is None:
                    failures.append(
                        OrderedDict((("code", "harness_runtime"), ("detail", name)))
                    )
                    continue
                measurement_failures = validate_harness_result(
                    measurement,
                    return_code,
                    entry["profile"],
                    fixture,
                    required_by_fixture[fixture],
                    minimum_population,
                    name,
                    args.width,
                    args.height,
                    expected_harness_source_sha256,
                )
                failures.extend(measurement_failures)
                tolerance_failure = _regular_tolerance_failure(
                    measurement, measurement_failures, name, fixture
                )
                if tolerance_failure is not None:
                    failures.append(tolerance_failure)
                fixture_results.append(
                    _fixture_result(
                        name,
                        entry["profile"],
                        entry["ibl_in_light"],
                        fixture,
                        measurement,
                    )
                )

        if args.mutation_suite == "required":
            entry_by_name = {entry["name"]: entry for entry in entries}
            relevant_mutations = _relevant_mutations(
                contracts["mutations"], set(entry_by_name)
            )
            for mutation in relevant_mutations:
                target = mutation["target"]
                entry = entry_by_name.get(target)
                if (
                    entry is None
                    or target not in corpus_paths
                    or target not in compiled
                ):
                    failures.append(
                        OrderedDict(
                            (
                                ("code", "mutation_prerequisite"),
                                ("detail", mutation["id"]),
                            )
                        )
                    )
                    mutation_results.append(
                        _mutation_result(
                            mutation,
                            None,
                            None,
                            minimum_population,
                            "mutation_prerequisite",
                        )
                    )
                    continue
                source_path = os.path.join(REPO_ROOT, entry["src"])
                original_bytes = Path(source_path).read_bytes()
                original_text = original_bytes.decode("utf-8-sig")
                try:
                    control_text, mutant_text = build_mutation_sources(
                        original_text, target, mutation["id"]
                    )
                except StableFailure as error:
                    failures.append(
                        OrderedDict((("code", error.code), ("detail", mutation["id"])))
                    )
                    mutation_results.append(
                        _mutation_result(
                            mutation,
                            None,
                            None,
                            minimum_population,
                            error.code,
                        )
                    )
                    continue
                if Path(source_path).read_bytes() != original_bytes:
                    failures.append(
                        OrderedDict(
                            (
                                ("code", "mutation_source_changed"),
                                ("detail", mutation["id"]),
                            )
                        )
                    )
                    mutation_results.append(
                        _mutation_result(
                            mutation,
                            None,
                            None,
                            minimum_population,
                            "mutation_source_changed",
                        )
                    )
                    continue
                mutation_dir = os.path.join(temporary, f"mutation-{mutation['id']}")
                os.makedirs(mutation_dir)
                control_source = os.path.join(mutation_dir, "control.hlsl")
                mutant_source = os.path.join(mutation_dir, "mutant.hlsl")
                Path(control_source).write_text(
                    control_text, encoding="utf-8", newline="\n"
                )
                Path(mutant_source).write_text(
                    mutant_text, encoding="utf-8", newline="\n"
                )
                control_dxbc = os.path.join(mutation_dir, "control.dxbc")
                mutant_dxbc = os.path.join(mutation_dir, "mutant.dxbc")
                control_ok, control_log = _compile_shader(
                    fxc,
                    control_source,
                    os.path.dirname(source_path),
                    entry["defines"],
                    control_dxbc,
                )
                mutant_ok, mutant_log = _compile_shader(
                    fxc,
                    mutant_source,
                    os.path.dirname(source_path),
                    entry["defines"],
                    mutant_dxbc,
                )
                if not control_ok or not mutant_ok:
                    log = control_log if not control_ok else mutant_log
                    if log.strip():
                        print(log.rstrip(), file=sys.stderr)
                    failed_kind = "control" if not control_ok else "mutant"
                    failures.append(
                        OrderedDict(
                            (
                                ("code", "mutation_compile_failed"),
                                ("detail", f"{mutation['id']}:{failed_kind}"),
                            )
                        )
                    )
                    mutation_results.append(
                        _mutation_result(
                            mutation,
                            None,
                            None,
                            minimum_population,
                            "mutation_compile_failed",
                        )
                    )
                    continue
                required = contracts["profiles"][entry["profile"]][
                    "required_buckets"
                ][mutation["fixture"]]
                control_measurement, control_return_code, control_human = _run_harness(
                    args.exe,
                    corpus_paths[target],
                    control_dxbc,
                    mutation["fixture"],
                    entry["profile"],
                    required,
                    minimum_population,
                    args.seeds,
                    args.seed_base,
                    args.width,
                    args.height,
                    args.tol_abs,
                    args.tol_rel,
                    temporary,
                    f"control-{mutation['id']}",
                    args.verbose,
                )
                mutant_measurement, mutant_return_code, mutant_human = _run_harness(
                    args.exe,
                    corpus_paths[target],
                    mutant_dxbc,
                    mutation["fixture"],
                    entry["profile"],
                    required,
                    minimum_population,
                    args.seeds,
                    args.seed_base,
                    args.width,
                    args.height,
                    args.tol_abs,
                    args.tol_rel,
                    temporary,
                    f"mutant-{mutation['id']}",
                    args.verbose,
                )
                infrastructure_failure: str | None = None
                if control_measurement is None or mutant_measurement is None:
                    failures.append(
                        OrderedDict(
                            (("code", "harness_runtime"), ("detail", mutation["id"]))
                        )
                    )
                    infrastructure_failure = "harness_runtime"
                control_validation: list[OrderedDict[str, str]] = []
                if control_measurement is not None:
                    control_validation = validate_harness_result(
                        control_measurement,
                        control_return_code,
                        entry["profile"],
                        mutation["fixture"],
                        required,
                        minimum_population,
                        f"{mutation['id']}:control",
                        args.width,
                        args.height,
                        expected_harness_source_sha256,
                    )
                    failures.extend(control_validation)
                mutant_validation: list[OrderedDict[str, str]] = []
                if mutant_measurement is not None:
                    mutant_validation = validate_harness_result(
                        mutant_measurement,
                        mutant_return_code,
                        entry["profile"],
                        mutation["fixture"],
                        required,
                        minimum_population,
                        f"{mutation['id']}:mutant",
                        args.width,
                        args.height,
                        expected_harness_source_sha256,
                    )
                    failures.extend(mutant_validation)
                if control_validation or mutant_validation:
                    infrastructure_failure = "mutation_protocol"
                if (
                    infrastructure_failure is None
                    and control_measurement is not None
                    and int(
                        control_measurement.get("aggregate", {}).get(
                            "divergent_pixels", -1
                        )
                    )
                    != 0
                ):
                    failures.append(
                        OrderedDict(
                            (
                                ("code", "mutation_control_dirty"),
                                ("detail", mutation["id"]),
                            )
                        )
                    )
                    infrastructure_failure = "mutation_control_dirty"
                if args.verbose:
                    _print_human(
                        f"{mutation['id']} control",
                        control_measurement,
                        control_human,
                    )
                    _print_human(
                        f"{mutation['id']} mutant",
                        mutant_measurement,
                        mutant_human,
                    )
                mutation_results.append(
                    _mutation_result(
                        mutation,
                        control_measurement,
                        mutant_measurement,
                        minimum_population,
                        infrastructure_failure,
                        OrderedDict(
                            (
                                (
                                    "control_hlsl",
                                    OrderedDict(
                                        (
                                            (
                                                "sha256",
                                                hashlib.sha256(
                                                    control_text.encode("utf-8")
                                                ).hexdigest(),
                                            ),
                                            (
                                                "size",
                                                len(control_text.encode("utf-8")),
                                            ),
                                        )
                                    ),
                                ),
                                (
                                    "mutant_hlsl",
                                    OrderedDict(
                                        (
                                            (
                                                "sha256",
                                                hashlib.sha256(
                                                    mutant_text.encode("utf-8")
                                                ).hexdigest(),
                                            ),
                                            (
                                                "size",
                                                len(mutant_text.encode("utf-8")),
                                            ),
                                        )
                                    ),
                                ),
                                ("control_dxbc", _hash_file(control_dxbc)),
                                ("mutant_dxbc", _hash_file(mutant_dxbc)),
                            )
                        ),
                    )
                )

    fixture_results.sort(key=lambda item: (item["target"], item["fixture"]))
    mutation_results.sort(key=lambda item: item["id"])
    input_results.sort(key=lambda item: item["target"])
    failures = sorted(
        {json.dumps(item, sort_keys=True): item for item in failures}.values(),
        key=lambda item: (item["code"], item["detail"]),
    )
    aggregate = _merge_metrics(item["aggregate"] for item in fixture_results)
    complete_fixtures = args.fixture == "all"
    mutations_required = args.mutation_suite == "required"
    if not complete_fixtures:
        failures.append(
            OrderedDict(
                (
                    ("code", "fixture_incomplete"),
                    ("detail", args.fixture),
                )
            )
        )
    if not mutations_required:
        failures.append(
            OrderedDict((("code", "mutation_suite_skipped"), ("detail", "skip")))
        )
    failures = sorted(
        {json.dumps(item, sort_keys=True): item for item in failures}.values(),
        key=lambda item: (item["code"], item["detail"]),
    )
    verdict, exit_code = classify_result(
        complete_fixtures,
        mutations_required,
        failures,
        aggregate,
        mutation_results,
    )
    authoritative = (
        scope == "full"
        and args.fixture == "all"
        and args.mutation_suite == "required"
    )

    script_hash = _hash_file(__file__, include_sha1=False)
    contracts_hash = _hash_file(args.contracts, include_sha1=False)
    shaping_hash = hashlib.sha256(
        Path(args.contracts).read_bytes() + Path(HARNESS_SOURCE).read_bytes()
    ).hexdigest()
    producer = OrderedDict(
        (
            ("name", "shader_exec_diff.py"),
            ("version", TOOL_VERSION),
            ("script", script_hash),
            ("harness_version", HARNESS_VERSION),
            ("harness_source", harness_source_hash),
            ("harness_executable", _hash_file(args.exe, include_sha1=False)),
            ("harness_self_test", harness_self_test),
            ("shaping_profile_sha256", shaping_hash),
        )
    )
    compiler = OrderedDict(
        (
            ("name", "fxc"),
            ("banner", compiler_banner),
            ("version", compiler_version),
            ("binary", compiler_binary),
            ("flags", [*COMPILE_FLAGS, "/I", "original-source-directory"]),
        )
    )
    configuration = OrderedDict(
        (
            ("contracts", contracts_hash),
            ("scope", scope),
            ("authoritative", authoritative),
            ("targets", sorted(entry["name"] for entry in entries)),
            ("fixture", args.fixture),
            ("mutation_suite", args.mutation_suite),
            ("seeds", args.seeds),
            ("seed_base", args.seed_base),
            ("width", args.width),
            ("height", args.height),
            (
                "execution_environment",
                OrderedDict(
                    (
                        ("driver_type", "WARP"),
                        ("feature_level", "11_0"),
                        ("runtime_fingerprint", "system-d3d11-runtime"),
                        (
                            "limitation",
                            "runtime binary identity is external to this receipt",
                        ),
                        ("components", runtime_components),
                    )
                ),
            ),
            ("tolerance_absolute", args.tol_abs),
            ("tolerance_relative", args.tol_rel),
            ("minimum_bucket_population", minimum_population),
        )
    )
    report = _make_report(
        REPORT_SCHEMA,
        scope,
        authoritative,
        producer,
        compiler,
        configuration,
        input_results,
        fixture_results,
        mutation_results,
        aggregate,
        failures,
        verdict,
    )
    _assert_receipt_safe(report)
    current_bytes = _canonical_bytes(report)
    if args.check_report:
        if not check_report_bytes(args.check_report, current_bytes):
            report["verdict"] = "STALE"
            return report, 2
        return report, exit_code
    if args.json:
        _write_canonical(args.json, report)
    if args.manifest_dir:
        run_manifest = _make_report(
            RUN_SCHEMA,
            scope,
            authoritative,
            producer,
            compiler,
            configuration,
            input_results,
            fixture_results,
            mutation_results,
            aggregate,
            failures,
            verdict,
        )
        _assert_receipt_safe(run_manifest)
        _write_canonical(
            os.path.join(args.manifest_dir, MANIFEST_FILENAME), run_manifest
        )
    return report, exit_code


def main(arguments: list[str] | None = None) -> int:
    try:
        report, exit_code = run(arguments)
        if report.get("schema") == FEATURE_RUN_SCHEMA:
            print(
                f"{report['verdict']}: WetnessEffects additive-feature WARP "
                "validation (reconstructed stock vs reconstructed feature; "
                "no native bytecode)"
            )
        else:
            print(f"{report['verdict']}: shader execution diff")
        return exit_code
    except StableFailure as error:
        print(f"{error.code}: {error.detail}", file=sys.stderr)
        return error.exit_code
    except Exception as error:
        print(f"internal_failure: {error.__class__.__name__}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
