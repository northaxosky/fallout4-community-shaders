# `D3DCompile` static analysis (Fallout4RE-side campaign cross-reference)

Short sibling-repo summary; canonical writeup lives at
`Fallout4RE/Workspace/docs/d3dcompile-static-analysis.md`.

## TL;DR

The Fallout 4 engine **does not use `D3DCompile`** under any path
across OG/NG/AE: zero `d3dcompiler*` imports, zero d3dcompile-family
strings in any of the three exes. The "engine compiles HLSL at
runtime" hypothesis from `shader-corpus-survey.md` is definitively
ruled out.

In its place, the campaign discovered the actual mechanism: each of
the 3 "missing" deferred-stage PSes (composite, ambient/IBL,
sun-shadow) has a **close shape-and-size peer in `Shaders011.fxp`**.
The size deltas are tiny:

| Captured PS | Live size | Best peer | Peer size | Δ bytes |
|---|---:|---|---:|---:|
| composite `813c9acec23b...` | 3,172 | `861504f6dcbe...` (blob 3539) | 3,176 | **+4** |
| ambient/IBL `761d41008016...` | 9,164 | `7460585eaf76...` (blob 3559) | 9,228 | +64 |
| sun-shadow `46b911cb8053...` | 2,188 | `8fb709c2fdf0...` (blob 2147) | 2,220 | +32 |

The hypothesis: the engine selects a corpus blob and applies a
small byte-level patch (likely resource-slot renumbering) before
passing to `CreatePixelShader`. The 16-byte MD5 embedded in the
DXBC header makes any byte change reflect in the sha1, which is why
`shader-corpus-survey.md` saw shape-near-but-sha1-mismatch peers as
"unknown."

## What this means for the deferred-lighting reconstruction targets

- `shader-2122-analysis.md` "blob misidentified, composite source is
  runtime-generated" - **revised**: composite source is corpus blob
  3539 + a small patch.
- `shader-3147-analysis.md` "30+ shape-identical peer cluster, no
  canonical pick" - **partially revised**: 3147 itself is still
  not the canonical sun-light PS in any captured scene, but the
  ambient/IBL captured PS (eid 45345) DOES match blob 3559's
  shape-and-size, and the canonical ambient/IBL pick 3560 is still
  the closest "heaviest representative."
- `shader-3560-analysis.md` "HLSL still WIP, bilateral blur deferred"
  - **unchanged**: the analysis target is still valid; the
  canonical ambient/IBL PS in `Shaders011.fxp` is still blob 3560
  (or its close peer 3559). What changes is our understanding that
  the engine's runtime version is patched, not regenerated.

## What this means for the runtime catalog plugin

The `ShaderCatalog` plugin (spec at
`Fallout4RE/Workspace/docs/shader-catalog-plugin-spec.md`) Phase 2
was slated to hook `D3DCompile` to capture HLSL source. With
`D3DCompile` ruled out, Phase 2's value shifts:

- **Drop** the `D3DCompile` hook and the `compile_events` table.
- **Add** patch-vector recording at the `CreatePixelShader` hook:
  for each captured bytecode, compute sha1, look up the closest
  shape-and-size peer in the corpus, byte-diff, and record the
  diff in a new `patch_events` table.

This is a Phase 2 spec update on the Fallout4RE side. Both repos
unblock simultaneously: the static byte-diff investigation can
proceed in parallel; the plugin Phase 2 spec ships next session;
the plugin implementer adjusts the in-flight `features/ShaderCatalog/`
plan accordingly.

## Open per-shader items moved here from corpus-survey

The "what does the engine patch?" question replaces the
"where does the bytecode live?" question for each deferred-stage PS.
Each open item is now answerable by:

1. Open `FO4_frame5407.rdc` in RenderDoc, navigate to the relevant
   eid (45368 composite, 45345 ambient, 45401 sun-shadow), right-
   click the PS in Pipeline State -> Save Shader Bytecode.
2. Disassemble the saved DXBC via
   `python Fallout4RE/Workspace/tools/commands/disassemble-dxbc.py`.
3. Byte-diff against the corpus peer's bytes
   (`Scratch/shaders-extracted/ShadersFX/Shaders011.fxp` at the
   peer blob's `container_offset`).
4. If the diff is small and patterned (e.g. `dcl_resource_texture2d
   t0` -> `dcl_resource_texture2d t4`), the patch set is publicly
   readable and can be encoded as a transformation.

## Cross-references

- Canonical: `Fallout4RE/Workspace/docs/d3dcompile-static-analysis.md`
- Negative-finding artifact:
  `Fallout4RE/Workspace/exports/cs-d3dcompile-call-sites.json`
- Corpus survey (now superseded in part):
  `shaders/lighting/shader-corpus-survey.md`
- Per-shader analyses:
  - `shaders/lighting/shader-2122-analysis.md`
  - `shaders/lighting/shader-3147-analysis.md`
  - `shaders/lighting/shader-3560-analysis.md`
- Catalog plugin spec (Phase 2 reframe needed):
  `Fallout4RE/Workspace/docs/shader-catalog-plugin-spec.md`
