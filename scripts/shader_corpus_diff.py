#!/usr/bin/env python3
"""Measure how closely a reconstructed deferred-pipeline HLSL matches the
original Fallout 4 shader ASM.

This is the fidelity counterpart to verify-shader-roundtrip.ps1. The roundtrip
script only pins our own fxc output against a committed sha1 (accidental-drift
guard, runs hermetically in CI). This tool compares our HLSL against the *real*
game shader by disassembling both and diffing the normalized instruction stream.
It needs the game corpus (gitignored, extracted by fetch-shader-corpus.ps1) so it
is a local dev/validation tool, not a CI gate.

Two layers are reported per shader:

  1. CONTRACT (hard PASS/FAIL) -- the drop-in requirement. Resource/sampler/CB
     register bindings, input/output signatures, and declared resource types must
     match exactly. A mismatch here means the shader is not a legal replacement
     (wrong register = wrong texture bound), regardless of how similar the math is.

  2. STREAM (informational) -- instruction-count delta, difflib similarity, sample
     op count, and (with -v) the aligned diff hunks that show which math differs.
     The game was built with a different/older fxc than ours, so even a perfect
     reconstruction will not reach 0% delta; the aligned hunks are the truth, the
     percentage is a guide.

Exit codes: 0 all compared shaders pass contract; 1 a contract FAIL or a
compile/disasm error; 2 nothing could be compared (corpus missing).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from difflib import SequenceMatcher

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_MANIFEST = os.path.join(REPO_ROOT, "scripts", "shader-roundtrip-baselines.json")
DEFAULT_CORPUS_DIR = os.path.join(REPO_ROOT, ".corpus")
DEFAULT_FXC = r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"

PROFILE_RE = re.compile(r"^(ps|vs|cs|gs|hs|ds)_\d+_\d+\b")
FLOAT_RE = re.compile(r"-?\d+\.\d+(?:e[+-]?\d+)?")
TEMP_REG_RE = re.compile(r"\br\d+\b")
ICB_TUPLE_RE = re.compile(r"\{\s*([^{}]*?)\s*\}")

CONTRACT_DECL_PREFIXES = (
    "dcl_constantbuffer",
    "dcl_sampler",
    "dcl_resource",
    "dcl_uav",
    "dcl_input",
    "dcl_output",
)


@dataclass
class Asm:
    profile: str = ""
    input_sig: list[str] = field(default_factory=list)
    output_sig: list[str] = field(default_factory=list)
    contract_decls: list[str] = field(default_factory=list)
    soft_decls: list[str] = field(default_factory=list)
    icb_entries: list[str] = field(default_factory=list)
    instructions: list[str] = field(default_factory=list)  # raw lines
    tokens: list[str] = field(default_factory=list)         # normalized, aligned 1:1 with instructions

    @property
    def sample_ops(self) -> int:
        return sum(1 for i in self.instructions
                   if i.lstrip().split(" ", 1)[0].startswith(("sample", "gather", "ld")))


def normalize_operands(operands: str) -> str:
    """Canonicalize an instruction's operands so the token is stable under
    temp-register allocation churn but sensitive to real differences.

    Kept (semantic): opcode, constant-buffer bank+element (cbN[i]), texture /
    sampler / input / output register identities, immediate literal values, and
    ALL swizzles / write-masks -- component selection (cb2[1].x vs .w) and
    destination masks are semantic and must not be erased, or the stream diff
    would report false agreement.

    Normalized away (compiler noise): temp-register *indices* only (r17 -> r);
    the temp's swizzle is preserved.
    """
    # Round float literals first (before touching registers).
    operands = FLOAT_RE.sub(lambda m: f"{float(m.group(0)):.3f}", operands)
    # Normalize temp-register indices; keep cbN / tN / sN / vN / oN identities
    # and every swizzle / write-mask intact.
    operands = TEMP_REG_RE.sub("r", operands)
    # Collapse whitespace.
    operands = re.sub(r"\s+", " ", operands).strip()
    return operands


def normalize_instruction(line: str) -> str:
    line = line.strip()
    parts = line.split(" ", 1)
    op = parts[0]
    operands = normalize_operands(parts[1]) if len(parts) > 1 else ""
    return f"{op} {operands}".strip()


def _parse_signature_block(lines: list[str], start: int) -> list[str]:
    """Parse a '// Name Index Mask Register SysValue Format Used' comment table
    that begins after `start`. Returns one canonical string per row."""
    rows: list[str] = []
    i = start
    # Skip the header separator lines until the first data row.
    seen_header = False
    while i < len(lines):
        raw = lines[i]
        if not raw.startswith("//"):
            break
        body = raw[2:].strip()
        i += 1
        if not body:
            if seen_header:
                break  # blank comment line ends the table
            continue
        if body.startswith("Name") or set(body) <= set("- "):
            seen_header = True
            continue
        # Data row: collapse whitespace into a stable tuple. Keep only the ABI
        # columns (Name, Index, Mask, Register, SysValue, Format); the trailing
        # "Used" column reflects which components the body reads -- a STREAM
        # concern, not the binding contract -- so it is excluded here.
        cols = body.split()
        if cols:
            rows.append(" ".join(cols[:6]))
    return rows


def parse_asm(text: str) -> Asm:
    lines = text.splitlines()
    asm = Asm()

    # Signature tables (comment blocks before the profile line).
    for idx, line in enumerate(lines):
        s = line.strip()
        if s.startswith("// Input signature:"):
            asm.input_sig = _parse_signature_block(lines, idx + 1)
        elif s.startswith("// Output signature:"):
            asm.output_sig = _parse_signature_block(lines, idx + 1)

    # Locate the profile line; the shader body follows.
    body_start = None
    for idx, line in enumerate(lines):
        if PROFILE_RE.match(line.strip()):
            asm.profile = line.strip()
            body_start = idx + 1
            break
    if body_start is None:
        return asm

    i = body_start
    while i < len(lines):
        line = lines[i]
        s = line.strip()
        i += 1
        if not s or s.startswith("//"):
            continue
        if s.startswith("dcl_globalFlags"):
            continue
        if s.startswith("dcl_immediateConstantBuffer"):
            # Multi-line brace block; accumulate until brace balance returns to 0.
            block = [s]
            balance = s.count("{") - s.count("}")
            while balance > 0 and i < len(lines):
                nxt = lines[i]
                block.append(nxt.strip())
                balance += nxt.count("{") - nxt.count("}")
                i += 1
            joined = " ".join(block)
            # The outermost braces wrap the entries; inner {a,b,c,d} are entries.
            inner = joined[joined.find("{") + 1: joined.rfind("}")]
            asm.icb_entries = [m.group(1).strip() for m in ICB_TUPLE_RE.finditer(inner)]
            continue
        if s.startswith("dcl_"):
            if s.startswith(CONTRACT_DECL_PREFIXES):
                asm.contract_decls.append(s)
            else:
                asm.soft_decls.append(s)
            continue
        # Executable instruction.
        asm.instructions.append(s)
        asm.tokens.append(normalize_instruction(s))

    return asm


def _find_fxc(explicit: str | None) -> str:
    for cand in (explicit, os.environ.get("FXC_PATH"), DEFAULT_FXC):
        if cand and os.path.isfile(cand):
            return cand
    raise FileNotFoundError(
        "fxc.exe not found. Pass --fxc <path>, set FXC_PATH, or install the "
        "Windows 10 SDK (default: %s)." % DEFAULT_FXC)


def _run(cmd: list[str]) -> tuple[bool, str]:
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode == 0, (p.stdout or "") + (p.stderr or "")


def compile_hlsl(fxc: str, src: str, defines: list[str], out_asm: str) -> tuple[bool, str]:
    cmd = [fxc, "/nologo", "/T", "ps_5_0", "/O3", "/E", "main"]
    for d in defines:
        cmd += ["/D", d]
    cmd += ["/Fc", out_asm, src]
    return _run(cmd)


def disasm_blob(fxc: str, dxbc: str, out_asm: str) -> tuple[bool, str]:
    return _run([fxc, "/nologo", "/dumpbin", "/Fc", out_asm, dxbc])


def _set_diff(a: list[str], b: list[str]) -> tuple[list[str], list[str]]:
    """Return (only_in_a, only_in_b) treating inputs as multisets by string."""
    from collections import Counter
    ca, cb = Counter(a), Counter(b)
    only_a = sorted((ca - cb).elements())
    only_b = sorted((cb - ca).elements())
    return only_a, only_b


@dataclass
class Report:
    name: str
    defines: list[str]
    ok: bool = False
    contract_ok: bool = False
    error: str = ""
    lines: list[str] = field(default_factory=list)


def compare_shader(name: str, defines: list[str], corpus: Asm, mine: Asm) -> Report:
    r = Report(name=name, defines=defines)
    out = r.lines

    # ---- CONTRACT ----
    res_only_c, res_only_m = _set_diff(corpus.contract_decls, mine.contract_decls)
    in_only_c, in_only_m = _set_diff(corpus.input_sig, mine.input_sig)
    on_only_c, on_only_m = _set_diff(corpus.output_sig, mine.output_sig)
    contract_ok = not (res_only_c or res_only_m or in_only_c or in_only_m or on_only_c or on_only_m)
    r.contract_ok = contract_ok
    out.append(f"  CONTRACT: {'PASS' if contract_ok else 'FAIL'}")
    out.append(f"    bindings: corpus {len(corpus.contract_decls)} / mine {len(mine.contract_decls)}")
    for label, only_c, only_m in (
        ("resource/CB/sampler", res_only_c, res_only_m),
        ("input signature", in_only_c, in_only_m),
        ("output signature", on_only_c, on_only_m),
    ):
        if only_c or only_m:
            out.append(f"    {label} MISMATCH:")
            for x in only_c:
                out.append(f"      - corpus only: {x}")
            for x in only_m:
                out.append(f"      + mine only:   {x}")

    # ---- STREAM ----
    nc, nm = len(corpus.instructions), len(mine.instructions)
    delta = ((nm - nc) / nc * 100.0) if nc else 0.0
    sim = SequenceMatcher(None, corpus.tokens, mine.tokens, autojunk=False).ratio()
    out.append(f"  STREAM: corpus {nc} insn / mine {nm} insn  ({delta:+.1f}%)   similarity {sim:.3f}")
    out.append(f"    samples/loads: corpus {corpus.sample_ops} / mine {mine.sample_ops}")
    out.append(f"    temps: corpus [{_temps(corpus)}] mine [{_temps(mine)}]")
    if corpus.icb_entries or mine.icb_entries:
        note = " (mine = consumed subset)" if len(mine.icb_entries) < len(corpus.icb_entries) else ""
        out.append(f"    ICB entries: corpus {len(corpus.icb_entries)} / mine {len(mine.icb_entries)}{note}")

    r.ok = contract_ok
    return r


def _temps(a: Asm) -> str:
    for d in a.soft_decls:
        if d.startswith("dcl_temps"):
            return d.replace("dcl_temps", "").strip()
    return "?"


def hunks(corpus: Asm, mine: Asm, context: int = 2) -> list[str]:
    """Aligned diff of the raw instruction streams (real asm lines, not tokens)."""
    sm = SequenceMatcher(None, corpus.tokens, mine.tokens, autojunk=False)
    out: list[str] = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        out.append(f"  @@ corpus[{i1}:{i2}] mine[{j1}:{j2}] ({tag}) @@")
        for k in range(i1, i2):
            out.append(f"    - {corpus.instructions[k]}")
        for k in range(j1, j2):
            out.append(f"    + {mine.instructions[k]}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Corpus-match diff for reconstructed FO4 deferred shaders.")
    ap.add_argument("--manifest", default=DEFAULT_MANIFEST)
    ap.add_argument("--corpus-dir", default=DEFAULT_CORPUS_DIR)
    ap.add_argument("--fxc", default=None)
    ap.add_argument("--shader", action="append", help="Only diff shader(s) with this name (repeatable).")
    ap.add_argument("--json", dest="json_out", help="Write a machine-readable report here.")
    ap.add_argument("-v", "--verbose", action="store_true", help="Print aligned diff hunks.")
    args = ap.parse_args()

    try:
        fxc = _find_fxc(args.fxc)
    except FileNotFoundError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    with open(args.manifest, "r", encoding="utf-8-sig") as f:
        manifest = json.load(f)

    entries = [s for s in manifest["shaders"] if s.get("corpus_sha1")]
    if args.shader:
        entries = [s for s in entries if s["name"] in set(args.shader)]
    if not entries:
        print("No manifest entries with a corpus_sha1 to compare.", file=sys.stderr)
        return 2

    reports: list[Report] = []
    compared = 0
    with tempfile.TemporaryDirectory() as tmp:
        for s in entries:
            name = s["name"]
            defines = s.get("defines", [])
            print(f"== {name}  (defines: {', '.join(defines) or 'none'}) ==")
            r = Report(name=name, defines=defines)

            blob = os.path.join(args.corpus_dir, f"{name}.dxbc")
            if not os.path.isfile(blob):
                r.error = (f"corpus blob missing: {blob}\n"
                           f"     run: pwsh scripts/fetch-shader-corpus.ps1")
                print(f"  SKIP: {r.error}")
                reports.append(r)
                print()
                continue

            src = os.path.join(REPO_ROOT, s["src"])
            mine_asm_path = os.path.join(tmp, f"{name}.mine.asm")
            corp_asm_path = os.path.join(tmp, f"{name}.corpus.asm")

            ok, log = compile_hlsl(fxc, src, defines, mine_asm_path)
            if not ok:
                r.error = "our HLSL failed to compile:\n" + log
                print(f"  ERROR: {r.error}")
                reports.append(r)
                print()
                continue
            ok, log = disasm_blob(fxc, blob, corp_asm_path)
            if not ok:
                r.error = "corpus blob failed to disassemble:\n" + log
                print(f"  ERROR: {r.error}")
                reports.append(r)
                print()
                continue

            with open(corp_asm_path, encoding="utf-8") as f:
                corpus = parse_asm(f.read())
            with open(mine_asm_path, encoding="utf-8") as f:
                mine = parse_asm(f.read())

            r = compare_shader(name, defines, corpus, mine)
            print("\n".join(r.lines))
            if args.verbose:
                hk = hunks(corpus, mine)
                if hk:
                    print("  --- aligned diff (corpus - / mine +) ---")
                    print("\n".join(hk))
            reports.append(r)
            compared += 1
            print()

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump([{"name": r.name, "defines": r.defines, "contract_ok": r.contract_ok,
                        "error": r.error, "report": r.lines} for r in reports], f, indent=2)

    fails = [r for r in reports if r.error]
    contract_fails = [r for r in reports if not r.error and not r.contract_ok]
    if compared == 0:
        print("Nothing compared (corpus missing). Run scripts/fetch-shader-corpus.ps1.")
        return 2
    if fails:
        print(f"ERROR: {len(fails)} shader(s) could not be compared.")
        return 1
    if contract_fails:
        print(f"FAIL: {len(contract_fails)} shader(s) break the drop-in contract: "
              + ", ".join(r.name for r in contract_fails))
        return 1
    print(f"OK: {compared} shader(s) hold the drop-in contract. Stream deltas are advisory.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
