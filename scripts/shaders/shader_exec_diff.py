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

TOOL_VERSION = 3
HARNESS_VERSION = 3
REPORT_SCHEMA = "fo4cs.shader-exec-diff-report"
RUN_SCHEMA = "fo4cs.shader-exec-diff-run"
MEASUREMENT_SCHEMA = "fo4cs.shader-exec-measurement"
CONTRACTS_SCHEMA = "fo4cs.shader-exec-contracts"
DEFAULT_CONTRACTS = os.path.join(
    REPO_ROOT, "scripts", "shaders", "shader-exec-contracts.json"
)
DEFAULT_EXECUTABLE = os.path.join(REPO_ROOT, ".shader-cache", "shader_exec_diff.exe")
HARNESS_SOURCE = os.path.join(
    REPO_ROOT, "scripts", "shaders", "shader_exec_diff", "main.cpp"
)
MANIFEST_FILENAME = "shader-exec-diff-run.json"
COMPILE_FLAGS = ("/nologo", "/T", "ps_5_0", "/O3", "/E", "main")
FIXTURE_ORDER = ("adversarial", "native")
VERDICTS = ("PASS", "FAIL", "UNPROVEN", "STALE")


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
    return bool(
        re.search(r"(^|[\s\"'])[A-Za-z]:[\\/]", value)
        or value.startswith("\\\\")
        or (
            value.startswith("/")
            and ("/" in value[1:] or "\\" in value[1:])
        )
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
    if not isinstance(mutations, list) or len(mutations) != 7:
        raise StableFailure("contracts_schema", "seven mutations are required")
    mutation_ids = [entry.get("id") for entry in mutations]
    if len(mutation_ids) != len(set(mutation_ids)):
        raise StableFailure("contracts_schema", "mutations must be unique")
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
DIRECTIONAL_DEPTH_OLD = """// Insn 4-17: depth-based matrix select.
    // Per-row ternary matches corpus shape closer than `float4x4` ?:.
    bool isNearPath = (depth < 0.01);"""
DIRECTIONAL_DEPTH_NEW = """// Insn 4-17: depth-based matrix select.
    // Per-row ternary matches corpus shape closer than `float4x4` ?:.
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
    if target.startswith("bsdf_light_deferred_directional"):
        return _ensure_inclusive(
            source,
            DIRECTIONAL_DEPTH_OLD,
            DIRECTIONAL_DEPTH_NEW,
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
) -> list[OrderedDict[str, str]]:
    failures: list[OrderedDict[str, str]] = []
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
    caught = (
        infrastructure_failure is None
        and control is not None
        and mutant is not None
        and control_divergent == 0
        and population >= minimum_population
        and divergent_pixels > 0
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


def _parse_arguments(arguments: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="WARP execution diff for reconstructed FO4 deferred shaders."
    )
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--contracts", default=DEFAULT_CONTRACTS)
    parser.add_argument("--corpus-dir", default=DEFAULT_CORPUS_DIR)
    parser.add_argument("--fxc", default=None)
    parser.add_argument("--exe", default=DEFAULT_EXECUTABLE)
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
