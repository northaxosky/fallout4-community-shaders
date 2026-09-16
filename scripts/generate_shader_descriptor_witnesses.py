#!/usr/bin/env python3
"""Generate descriptor-driven shader identity witnesses from a proof report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


TARGETS = {
    "deferred_prepass": "kDeferredPrepass",
    "utility": "kUtility",
    "particle": "kParticle",
    "effect": "kEffect",
    "blood_splatter": "kBloodSplatter",
    "distant_tree": "kDistantTree",
    "face_customization": "kFaceCustomization",
    "imagespace": "kImageSpace",
    "bssky": "kBsSky",
    "bswater": "kBsWater",
    "bslighting": "kBsLighting",
    "bsdf_light": "kBsdfLight",
    "bsdf_composite": "kBsdfComposite",
    "df_tiled_lighting": "kDfTiledLighting",
}

STAGES = {
    "vertex": "kVertex",
    "pixel": "kPixel",
    "compute": "kCompute",
}

def normalize_prepass_key(stage: str, key: int) -> int:
    if stage == "vertex":
        mask = 0xFFFE27FF if key & 0x10800 == 0x10800 else 0xFFFF2FFF
        return key & mask
    if stage == "pixel":
        return key & 0xFFFFEFE7
    return key


def imagespace_name(row: dict) -> str:
    name = row["name"]
    if name.startswith("imagespace_t"):
        tap_count = int(name[len("imagespace_t") : len("imagespace_t") + 2])
        threshold = name.endswith("thr1")
        return f"IS{'BrightPass' if threshold else ''}Blur{tap_count}"
    if row["stock_sha1"] == "100bed432577b799f59c369f13ae9db6403fb76e":
        return "ISVLS"
    if row["stock_sha1"] == "597db6590c6769a0a6980c6a61125ffa96773fd7":
        return "ISVLS_Coord"
    return {
        "imagespace_u0c0": "ISGamma",
        "imagespace_u0c1": "ISGammaLinearize",
        "imagespace_u1c0": "ISGammaResize",
    }.get(name, "")


def imagespace_native_contract(row: dict) -> tuple[str, str, str, str, str, str]:
    name = row["name"]
    if name.startswith("imagespace_t"):
        tap_count = str(int(name[len("imagespace_t") : len("imagespace_t") + 2]))
        threshold = name.endswith("thr1")
        return (
            f"BSImagespaceShader{'BrightPass' if threshold else ''}Blur{tap_count}",
            "ISBlur",
            "TEXTAP",
            tap_count,
            "BRIGHTPASS" if threshold else "",
            "",
        )
    if row["stock_sha1"] == "100bed432577b799f59c369f13ae9db6403fb76e":
        return (
            "BSImagespaceShaderVLSSliceInterp",
            "ISVLS",
            "SLICE_INTERP",
            "",
            "",
            "",
        )
    if row["stock_sha1"] == "597db6590c6769a0a6980c6a61125ffa96773fd7":
        return (
            "BSImagespaceShaderVLSSliceCoord",
            "ISVLS_Coord",
            "",
            "",
            "",
            "",
        )
    image_space_vertex_contracts = {
        "89e56423886dc05ed3b2d1445a70f386c651ac38": (
            "BSImagespaceShaderCopy", "", "", "", "", ""
        ),
        "89b8678cd5673cfb7702b6f3271151646fddb0b0": (
            "BSImagespaceShaderFXAA", "ISFXAA", "", "", "", ""
        ),
        "c400b7735461b8dbe3a4e40fed0434cc8e32f323": (
            "BSLensFlareVis", "LensFlare", "VISIBILITY", "", "", ""
        ),
        "d16f4e89901826c25bb4bc4a89910222649d6db2": (
            "BSImagespaceShaderHUDGlassMarkers",
            "ISHUDGlass",
            "MARKERS",
            "",
            "",
            "",
        ),
    }
    if row["stock_sha1"] in image_space_vertex_contracts:
        return image_space_vertex_contracts[row["stock_sha1"]]
    return {
        "imagespace_u0c0": (
            "BSImagespaceShaderGammaCorrect", "ISGamma", "", "", "", ""
        ),
        "imagespace_u0c1": (
            "BSImagespaceShaderGammaLinearize",
            "ISGamma",
            "LINEARIZE",
            "",
            "",
            "",
        ),
        "imagespace_u1c0": (
            "BSImagespaceShaderGammaCorrectResize",
            "ISGamma",
            "RESIZE",
            "",
            "",
            "",
        ),
        "imagespace_indexrebase_2": (
            "IndexBufferOffsetCS", "", "", "", "", ""
        ),
        "imagespace_indexrebase_5": (
            "IndexBufferOffsetCS", "", "", "", "", ""
        ),
        "imagespace_indexrebase_6": (
            "IndexBufferOffsetCS", "", "", "", "", ""
        ),
    }.get(name, ("", "", "", "", "", ""))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("proof", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    report = json.loads(args.proof.read_text(encoding="utf-8"))
    lines = [
        "// Generated from build/proof/current-baseline-proof.json.",
        "// Every row passed native executable, signature, and resource-ABI proof.",
        "// PrePass descriptors are reachable normalized native stage-map keys.",
        "// A dead archive-only PrePass body is retained with runtimeReachable=false.",
        f"// AE240 BA2 sha256: {report['archive']['archive_sha256']}",
        f"// FXC sha256: {report['compiler']['sha256']}",
        f"// Routes: {len(report['rows'])} "
        f"({report['summary']['proof_tiers']['exact-executable']} exact, "
        f"{report['summary']['proof_tiers']['canonical-executable']} approved canonical).",
    ]
    for row in report["rows"]:
        occurrences = row["native"]["occurrences"]
        if row["target"] == "deferred_prepass":
            occurrence = next(
                (
                    item
                    for item in occurrences
                    if normalize_prepass_key(
                        row["stage"], item["shader_key"]
                    )
                    == item["shader_key"]
                ),
                None,
            )
            runtime_reachable = occurrence is not None
            if occurrence is None:
                occurrence = occurrences[0]
        else:
            occurrence = occurrences[0]
            runtime_reachable = True
        native_name = imagespace_name(row) if row["target"] == "imagespace" else ""
        native_class, source_group, macro1, value1, macro2, value2 = (
            imagespace_native_contract(row)
            if row["target"] == "imagespace"
            else ("", "", "", "", "", "")
        )
        defines = {
            name: value or "1"
            for item in row["defines"]
            for name, _, value in [item.partition("=")]
        }
        force_early_depth = defines.get("EARLYDEPTH") == "1"
        descriptor = occurrence["shader_key"]
        expected = row["runtime_stripped"]
        lines.append(
            "{ "
            f"cs::engine::ShaderInjectionTarget::{TARGETS[row['target']]}, "
            f"cs::engine::ShaderStage::{STAGES[row['stage']]}, "
            f"{descriptor}U, "
            f'"{native_name}", '
            f'"{native_class}", '
            f"{'true' if force_early_depth else 'false'}, "
            f"{'true' if runtime_reachable else 'false'}, "
            f'"{source_group}", '
            f'"{macro1}", "{value1}", '
            f'"{macro2}", "{value2}", '
            f'"{row["stock_sha1"]}", '
            "{ "
            f"{expected['byte_length']}, "
            f'"{expected["sha1"]}", '
            f'"{expected["sha256"]}" '
            "} },"
        )

    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
