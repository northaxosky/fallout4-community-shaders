#!/usr/bin/env python3
"""Compile and execute the AO-only XeGTAO differential test."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

from shader_exec_diff import DEFAULT_EXECUTABLE, REPO_ROOT, _find_fxc


UPSTREAM_ROOT = os.path.join(
    REPO_ROOT, ".agents", "references", "skyrim-community-shaders"
)
UPSTREAM_SHADER = os.path.join(
    UPSTREAM_ROOT,
    "features",
    "Screen Space GI",
    "Shaders",
    "ScreenSpaceGI",
    "gi.cs.hlsl",
)
UPSTREAM_PREFILTER_SHADER = os.path.join(
    UPSTREAM_ROOT,
    "features",
    "Screen Space GI",
    "Shaders",
    "ScreenSpaceGI",
    "prefilterDepths.cs.hlsl",
)
CANDIDATE_ROOT = os.path.join(
    REPO_ROOT, "features", "ScreenSpaceGI", "Shaders"
)
CANDIDATE_SHADER = os.path.join(CANDIDATE_ROOT, "XeGTAO", "gi.cs.hlsl")
PREFILTER_SHADER = os.path.join(
    CANDIDATE_ROOT, "XeGTAO", "prefilterDepths.cs.hlsl"
)


def _compile(
    fxc: str,
    source: str,
    output: str,
    include_paths: list[str],
    defines: list[str],
) -> None:
    command = [fxc, "/nologo", "/T", "cs_5_0", "/O3", "/E", "main"]
    for include_path in include_paths:
        command += ["/I", include_path]
    for define in defines:
        command += ["/D", define]
    command += ["/Fo", output, source]
    process = subprocess.run(command, capture_output=True, text=True)
    if process.returncode != 0:
        log = (process.stdout or "") + (process.stderr or "")
        raise RuntimeError(f"failed to compile {source}:\n{log}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="WARP differential test for the AO-only XeGTAO port."
    )
    parser.add_argument("--fxc")
    parser.add_argument("--exe", default=DEFAULT_EXECUTABLE)
    parser.add_argument("--width", type=int, default=64)
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--tol-abs", type=float, default=1.0e-6)
    parser.add_argument("--tol-rel", type=float, default=1.0e-6)
    args = parser.parse_args()

    if not os.path.isfile(args.exe):
        print(f"ERROR: execution harness missing: {args.exe}", file=sys.stderr)
        print(
            "Build it with: pwsh scripts/shaders/shader_exec_diff/build.ps1",
            file=sys.stderr,
        )
        return 2
    for upstream_source in (UPSTREAM_SHADER, UPSTREAM_PREFILTER_SHADER):
        if not os.path.isfile(upstream_source):
            print(
                f"ERROR: upstream reference missing: {upstream_source}",
                file=sys.stderr,
            )
            return 2

    try:
        fxc = _find_fxc(args.fxc)
        with tempfile.TemporaryDirectory() as temporary_directory:
            upstream_bytecode = os.path.join(
                temporary_directory, "xegtao-upstream.dxbc"
            )
            candidate_bytecode = os.path.join(
                temporary_directory, "xegtao-candidate.dxbc"
            )
            upstream_prefilter_bytecode = os.path.join(
                temporary_directory, "xegtao-upstream-prefilter.dxbc"
            )
            candidate_prefilter_bytecode = os.path.join(
                temporary_directory, "xegtao-candidate-prefilter.dxbc"
            )
            _compile(
                fxc,
                UPSTREAM_SHADER,
                upstream_bytecode,
                [
                    os.path.join(UPSTREAM_ROOT, "package", "Shaders"),
                    os.path.join(
                        UPSTREAM_ROOT,
                        "features",
                        "Screen Space GI",
                        "Shaders",
                    ),
                ],
                ["CSHADER"],
            )
            _compile(
                fxc,
                CANDIDATE_SHADER,
                candidate_bytecode,
                [CANDIDATE_ROOT],
                [],
            )
            _compile(
                fxc,
                UPSTREAM_PREFILTER_SHADER,
                upstream_prefilter_bytecode,
                [
                    os.path.join(UPSTREAM_ROOT, "package", "Shaders"),
                    os.path.join(
                        UPSTREAM_ROOT,
                        "features",
                        "Screen Space GI",
                        "Shaders",
                    ),
                ],
                ["CSHADER", "LINEAR_FILTER"],
            )
            _compile(
                fxc,
                PREFILTER_SHADER,
                candidate_prefilter_bytecode,
                [CANDIDATE_ROOT],
                ["LINEAR_FILTER"],
            )

            print(
                f"Tolerance: abs={args.tol_abs:g}, rel={args.tol_rel:g} "
                "(identical fxc math is expected to be exact).",
                flush=True,
            )
            command = [
                args.exe,
                upstream_bytecode,
                candidate_bytecode,
                "--xegtao-ao",
                "--reference-prefilter",
                upstream_prefilter_bytecode,
                "--candidate-prefilter",
                candidate_prefilter_bytecode,
                "--width",
                str(args.width),
                "--height",
                str(args.height),
                "--tol-abs",
                str(args.tol_abs),
                "--tol-rel",
                str(args.tol_rel),
            ]
            return subprocess.run(command).returncode
    except (FileNotFoundError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
