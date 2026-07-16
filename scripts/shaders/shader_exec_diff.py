#!/usr/bin/env python3
"""Execute reconstructed and corpus shaders on identical D3D11 WARP inputs."""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile

from shader_corpus_diff import (
    DEFAULT_CORPUS_DIR,
    DEFAULT_FXC,
    DEFAULT_MANIFEST,
    REPO_ROOT,
)

DEFAULT_EXECUTABLE = os.path.join(REPO_ROOT, ".shader-cache", "shader_exec_diff.exe")


def _find_fxc(explicit: str | None) -> str:
    for candidate in (explicit, os.environ.get("FXC_PATH"), DEFAULT_FXC):
        if candidate and os.path.isfile(candidate):
            return candidate
    raise FileNotFoundError(
        "fxc.exe not found. Pass --fxc <path>, set FXC_PATH, or install the "
        f"Windows 10 SDK (default: {DEFAULT_FXC})."
    )


def _positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def _nonnegative_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return parsed


def _compile_shader(
    fxc: str, source: str, defines: list[str], output: str
) -> tuple[bool, str]:
    command = [fxc, "/nologo", "/T", "ps_5_0", "/O3", "/E", "main"]
    for define in defines:
        command += ["/D", define]
    command += ["/Fo", output, source]
    process = subprocess.run(command, capture_output=True, text=True)
    log = (process.stdout or "") + (process.stderr or "")
    return process.returncode == 0, log


def _indent(text: str) -> str:
    return "\n".join(f"  {line}" for line in text.rstrip().splitlines())


def main() -> int:
    parser = argparse.ArgumentParser(
        description="WARP execution diff for reconstructed FO4 deferred shaders."
    )
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--corpus-dir", default=DEFAULT_CORPUS_DIR)
    parser.add_argument("--fxc", default=None)
    parser.add_argument("--exe", default=DEFAULT_EXECUTABLE)
    parser.add_argument(
        "--shader",
        action="append",
        help="Only diff shader(s) with this name (repeatable).",
    )
    parser.add_argument("--seeds", type=_positive_int, default=8)
    parser.add_argument("--width", type=_positive_int, default=256)
    parser.add_argument("--height", type=_positive_int, default=256)
    parser.add_argument("--tol-abs", type=_nonnegative_float, default=2e-3)
    parser.add_argument("--tol-rel", type=_nonnegative_float, default=1e-2)
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Print worst divergent pixels."
    )
    args = parser.parse_args()

    if not os.path.isfile(args.exe):
        print(f"ERROR: execution harness missing: {args.exe}", file=sys.stderr)
        print(
            "Build it with: pwsh scripts/shaders/shader_exec_diff/build.ps1",
            file=sys.stderr,
        )
        return 2

    try:
        fxc = _find_fxc(args.fxc)
    except FileNotFoundError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    with open(args.manifest, "r", encoding="utf-8-sig") as stream:
        manifest = json.load(stream)

    entries = [shader for shader in manifest["shaders"] if shader.get("corpus_sha1")]
    if args.shader:
        requested = set(args.shader)
        entries = [shader for shader in entries if shader["name"] in requested]
    if not entries:
        print("No manifest entries with a corpus_sha1 to compare.", file=sys.stderr)
        return 2

    compared = 0
    divergences: list[str] = []
    advisories: list[str] = []
    errors: list[str] = []
    with tempfile.TemporaryDirectory() as temporary_directory:
        for shader in entries:
            name = shader["name"]
            defines = shader.get("defines", [])
            print(f"== {name}  (defines: {', '.join(defines) or 'none'}) ==")

            corpus = os.path.join(args.corpus_dir, f"{name}.dxbc")
            if not os.path.isfile(corpus):
                print(f"  SKIP: corpus blob missing: {corpus}")
                print("        run: pwsh scripts/shaders/fetch-shader-corpus.ps1")
                print()
                continue

            source = os.path.join(REPO_ROOT, shader["src"])
            candidate = os.path.join(temporary_directory, f"{name}.dxbc")
            compiled, compile_log = _compile_shader(
                fxc, source, defines, candidate
            )
            if not compiled:
                print("  ERROR: reconstructed HLSL failed to compile:")
                print(_indent(compile_log))
                errors.append(name)
                print()
                continue

            command = [
                args.exe,
                corpus,
                candidate,
                "--seeds",
                str(args.seeds),
                "--width",
                str(args.width),
                "--height",
                str(args.height),
                "--tol-abs",
                str(args.tol_abs),
                "--tol-rel",
                str(args.tol_rel),
            ]
            if args.verbose:
                command.append("--verbose")
            process = subprocess.run(command, capture_output=True, text=True)
            output = (process.stdout or "") + (process.stderr or "")
            profile = "unshaped"
            for line in output.splitlines():
                stripped = line.strip()
                if stripped.startswith("input profile:"):
                    profile = stripped.partition(":")[2].strip()
                    break
            shaped = profile != "unshaped"
            print(
                f"  INPUTS: {'SHAPED (' + profile + ')' if shaped else 'UNSHAPED (advisory)'}"
            )

            if process.returncode == 0:
                verdict = "PASS" if shaped else "ADVISORY (pass on unshaped inputs)"
            elif process.returncode == 1:
                if shaped:
                    verdict = "DIVERGE"
                    divergences.append(name)
                else:
                    verdict = "ADVISORY (diverge on unshaped inputs)"
                    advisories.append(name)
            else:
                verdict = "ERROR"
                errors.append(name)

            print(f"  VERDICT: {verdict}")
            if output.strip():
                print(_indent(output))
            compared += 1
            print()

    if compared == 0:
        print(
            "Nothing compared (corpus missing). "
            "Run scripts/shaders/fetch-shader-corpus.ps1."
        )
        return 2
    if errors:
        print(f"ERROR: {len(errors)} shader(s) could not be compared: {', '.join(errors)}")
        return 1
    if divergences:
        print(f"DIVERGE: {len(divergences)} shader(s): {', '.join(divergences)}")
        return 1
    print("PASS: no shaped-contract divergences.")
    if advisories:
        print(
            f"ADVISORY: {len(advisories)} unshaped shader(s) diverged: "
            f"{', '.join(advisories)}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
