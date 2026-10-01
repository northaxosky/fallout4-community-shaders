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
    document = json.loads(raw)
    if "bodies" not in document:
        for native in document["unit"]["pixel_routes"]:
            if (native["full_dxbc_sha1"] == route["stock_sha1"]
                    and native["fxp_key"] == route["descriptor"]
                    and native["fxp_ordinal"] == route["fxp_ordinal"]):
                require(route["stage"] == "pixel", "receipt stage mismatch")
                matches.add(document["source"]["routes"][native["selector"]]["residual"]["candidate_sha1"])
    for body in document.get("bodies", []):
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
            and document["schema_version"] == 2, "unsupported export schema/version")
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

    excluded = Counter(dict.fromkeys(("unhooked",), 0))
    tiers = Counter(dict.fromkeys(("exact", "canonical", "unproven"), 0))
    rows = []
    unnamed = 0
    seen = set()
    for route in routes:
        key = (route["target"], route["stage"], route["descriptor"], route["fxp_ordinal"])
        native_name = route["native_name"]
        require(native_name is None or (
            isinstance(native_name, str) and re.fullmatch("[A-Za-z][A-Za-z0-9_]{0,63}", native_name)
        ), f"invalid native name: {key}")
        require(type(route["hooked"]) is bool, f"invalid hooked: {key}")
        reason = "unhooked" if not route["hooked"] else None
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
        identity = route["imagespace"]
        require(not identity or identity["native_name"] == native_name,
                f"native identity mismatch: {key}")
        names = [native_name or "", *(
            identity[k] if identity else "" for k in ("class_name", "source_group")
        )]
        macros = identity["macros"] if identity else []
        require(all(isinstance(s, str) for s in names), f"invalid native identity: {key}")
        require(all(len(m) == 2 and all(isinstance(s, str) for s in m) for m in macros),
                f"invalid native macros: {key}")
        quoted = "\t".join(json.dumps(s, ensure_ascii=False) for s in names)
        quoted += f"\t{len(macros)}"
        for name, value in macros:
            quoted += "\t" + json.dumps(name) + "\t" + json.dumps(value)
        rows.append((*key, int(bool(route["early_depth"])), expected, quoted))
        unnamed += native_name is None
        tiers[route["tier"]] += 1
    compiler = document["compiler"]["d3dcompiler_47"]
    require(compiler["strip"] == "D3DCOMPILER_STRIP_REFLECTION_DATA", "unexpected stripping policy")
    require(re.fullmatch("[0-9a-f]{64}", compiler["sha256"]), "invalid compiler SHA-256")
    header = {
        "export_sha256": hashlib.sha256(raw).hexdigest(),
        **{k: declared["total"][k] for k in ("routes", "exact", "canonical", "unproven")},
        "rows": len(rows),
        **{f"rows_{k}": v for k, v in tiers.items()},
        "rows_unnamed": unnamed,
        **{f"excluded_{k}": v for k, v in excluded.items()},
        "compiler_sha256": compiler["sha256"],
    }
    text = "# " + " ".join(f"{k}={v}" for k, v in header.items()) + "\n"
    text += "".join(f"{target}\t{stage}\t0x{descriptor:08x}\t{early}\t{sha1}\t{ordinal}\t{identity}\n"
                    for target, stage, descriptor, ordinal, early, sha1, identity in sorted(rows))
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
