import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "tests/data"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def counts(routes):
    result = dict.fromkeys(
        ("routes", "exact", "canonical", "unproven",
         "reached_true", "reached_false", "reached_null"), 0
    )
    for route in routes:
        require(route["tier"] in ("exact", "canonical", "unproven"), "unknown tier")
        require(type(route["reached"]) in (bool, type(None)), "invalid reached")
        result["routes"] += 1
        result[route["tier"]] += 1
        result["reached_" + {True: "true", False: "false", None: "null"}[route["reached"]]] += 1
    return result


def candidate(route, export_root):
    receipt = route["producer_receipt"]
    raw = (export_root / receipt["path"]).read_bytes()
    require(hashlib.sha256(raw).hexdigest() == receipt["sha256"], "receipt hash mismatch")
    matches = set()
    for body in json.loads(raw)["bodies"]:
        for container in body["containers"]:
            if container["full_dxbc_sha1"] != route["stock_sha1"]:
                continue
            for compilation in container["compile_classes"]:
                if any(r["fxp_key"] == route["descriptor"]
                       and r["fxp_ordinal"] == route["fxp_ordinal"]
                       for r in compilation["routes"]):
                    require(body["stage"] == {"vertex": "vs", "pixel": "ps", "compute": "cs"}[route["stage"]],
                            "receipt stage mismatch")
                    matches.add(compilation["candidate"]["full_dxbc_sha1"])
    require(len(matches) == 1, f"missing or ambiguous candidate: {route['target']} {route['descriptor']}")
    return matches.pop()


def generate(export):
    raw = export.read_bytes()
    document = json.loads(raw)
    require(document["schema"] == "fo4re.consumer-stock-identity"
            and document["schema_version"] == 1, "unsupported export schema/version")
    routes = document["routes"]
    declared = document["counts"]
    require(counts(routes) == declared["total"], "total counts mismatch")
    require({r["target"] for r in routes} == set(declared["by_target"]), "target counts mismatch")
    for target, summary in declared["by_target"].items():
        group = [r for r in routes if r["target"] == target]
        require(counts(group) == summary["totals"], f"{target}: counts mismatch")
        require({r["stage"] for r in group} == set(summary["stages"]), f"{target}: stage counts mismatch")
        for stage, expected in summary["stages"].items():
            require(counts([r for r in group if r["stage"] == stage]) == expected,
                    f"{target} {stage}: counts mismatch")
    require(sum(r["imagespace"] is not None for r in routes if r["target"] == "imagespace")
            == declared["imagespace_identity_established"], "imagespace count mismatch")

    targets = set(re.findall(
        r'\{ ShaderInjectionTarget::\w+, "([^"]+)"',
        (ROOT / "src/Render/ShaderInjectionTargets.h").read_text()
    ))
    require(targets, "no production shader targets found")
    excluded = Counter(dict.fromkeys(("imagespace", "unowned", "unhooked"), 0))
    tiers = Counter(dict.fromkeys(("exact", "canonical", "unproven"), 0))
    rows = []
    seen = set()
    for route in routes:
        key = (route["target"], route["stage"], route["descriptor"])
        require(type(route["hooked"]) is bool, f"invalid hooked: {key}")
        reason = (
            "imagespace" if route["target"] == "imagespace" else
            "unowned" if route["target"] not in targets else
            "unhooked" if not route["hooked"] else None
        )
        if reason:
            excluded[reason] += 1
            continue
        require(key not in seen, f"duplicate route: {key}")
        seen.add(key)
        require(route["stage"] in ("vertex", "pixel", "compute"), f"unsupported stage: {key}")
        require(type(route["descriptor"]) is int and 0 <= route["descriptor"] <= 0xffffffff,
                f"invalid descriptor: {key}")
        require(type(route["early_depth"]) in (bool, type(None)), f"invalid early_depth: {key}")
        expected = (candidate(route, export.parent.parent.parent) if route["tier"] == "canonical"
                    else route["stock_sha1"])
        require(re.fullmatch("[0-9a-f]{40}", expected), f"invalid SHA-1: {key}")
        rows.append((*key, int(bool(route["early_depth"])), expected))
        tiers[route["tier"]] += 1
    compiler = document["compiler"]["d3dcompiler_47"]
    require(compiler["strip"] == "D3DCOMPILER_STRIP_REFLECTION_DATA", "unexpected stripping policy")
    require(re.fullmatch("[0-9a-f]{64}", compiler["sha256"]), "invalid compiler SHA-256")
    header = {
        "export_sha256": hashlib.sha256(raw).hexdigest(),
        **{k: declared["total"][k] for k in ("routes", "exact", "canonical", "unproven")},
        "gated": len(rows),
        **{f"gated_{k}": v for k, v in tiers.items()},
        **{f"excluded_{k}": v for k, v in excluded.items()},
        "compiler_sha256": compiler["sha256"],
    }
    text = "# " + " ".join(f"{k}={v}" for k, v in header.items()) + "\n"
    text += "".join(f"{target}\t{stage}\t0x{descriptor:08x}\t{early}\t{sha1}\n"
                    for target, stage, descriptor, early, sha1 in sorted(rows))
    (DATA / "stock-shader-identity.tsv").write_text(text, encoding="utf-8", newline="\n")
    print(text.splitlines()[0])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate the stock shader identity gate table.")
    parser.add_argument("export", type=Path)
    args = parser.parse_args()
    try:
        generate(args.export)
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(1, f"stock identity export rejected: {error}\n")
