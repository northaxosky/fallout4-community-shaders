# `D3DCompile` static analysis (Fallout4RE-side campaign cross-reference)

Short sibling-repo summary; canonical writeup lives at
`Fallout4RE/Workspace/docs/d3dcompile-static-analysis.md`.

## TL;DR (after same-day byte-diff follow-up)

The Fallout 4 engine **does not use `D3DCompile`** under any path
across OG/NG/AE: zero `d3dcompiler*` imports, zero d3dcompile-family
strings in any of the three exes. The "engine compiles HLSL at
runtime" hypothesis from `shader-corpus-survey.md` is definitively
ruled out.

The 3 captured "missing" deferred-stage PSes were then matched
against shape-and-size-near corpus peers, then dumped from
RenderDoc and byte-diffed. **The byte-diff was misleading (46-53%
of bytes differ) but the mnemonic-stream diff is conclusive**:

| Pair | Runtime insns / samples | Corpus insns / samples | Δ |
|---|---|---|---|
| composite eid 45368 vs blob 3539 | 90 / 6 | 90 / 6 | **0 / 0** |
| ambient/IBL eid 45345 vs blob 3559 | 263 / 44 | 265 / 44 | -2 / 0 |
| sun-shadow eid 45401 vs blob 2147 | 62 / 1 | 62 / 1 | **0 / 0** |

The corpus contains functionally-identical (or near-identical)
versions of all 3 deferred-stage PSes. The byte-level differences
are explained by register-allocation choices during DXBC encoding -
same shader, different bytecode encoding. Resource declarations and
control flow are identical between runtime and corpus peer.

## What this means for the deferred-lighting reconstruction targets

**Fully revised** in light of the byte-diff follow-up:

- `shader-2122-analysis.md` "blob misidentified, composite source
  is runtime-generated" - the composite is corpus blob 3539
  (`861504f6dcbe...`). The 2122 Phase A pick was wrong, but the
  right canonical IS in the on-disk corpus.
- `shader-3560-analysis.md` "HLSL still WIP, bilateral blur
  deferred" - canonical ambient/IBL is still blob 3560 (or its
  close peer 3559 `7460585eaf76...`); the runtime-stream
  equivalence confirms the reconstruction target.
- `shader-3147-analysis.md` "30+ peer cluster, not confirmed
  canonical" - the captured sun-shadow PS at eid 45401 matches
  corpus blob 2147 (`8fb709c2fdf0...`) exactly at instruction
  count + sample count. Blob 3147 is a different PS - the Phase A
  heaviest-representative pick for the directional-sun-light
  role doesn't match any captured PS in this scene. The sun-shadow
  reconstruction target is blob 2147.

Stage 2 reconstruction (per the runbook) can proceed directly against
the corpus blobs. The previous "blocked on bytecode-source location"
story is closed.

## What this means for the runtime catalog plugin

The `ShaderCatalog` plugin (spec at
`Fallout4RE/Workspace/docs/shader-catalog-plugin-spec.md`) Phase 2
was originally going to hook `D3DCompile`. With `D3DCompile` ruled
out AND the corpus-equivalence finding making patch-vector capture
moot, **Phase 2 is reframed**:

- **Drop** the `D3DCompile` hook entirely (deprecated in the spec).
- **Drop** the patch-vector idea (patches aren't the mechanism).
- **Add (Phase 2, schema v2)** a `corpus_match_sha1` column on
  `shader_catalog`, populated by a writer-thread mnemonic-stream
  match against `Fallout4RE/.cache/shader-corpus.sqlite`. This
  joins runtime-loaded shaders to corpus blobs deterministically.

The Phase 1 hook surface (5 device-vtable slots, observe-only)
remains correct and unchanged. Phase 2 is now purely additive (one
column + one writer-thread routine) rather than a different hook
surface.

## Open per-shader items moved here from corpus-survey

The "what does the engine patch?" question is resolved (no patching
happens). The reconstruction work for each deferred-stage PS now
operates directly on the corpus blob:

1. **composite**: open
   `Scratch/shaders-extracted/ShadersFX/index/Shaders011/asm/Shaders011.3539.861504f6dcbe.dxbc.asm`
   and reverse-engineer the HLSL per the runbook's Stage 2.
2. **ambient/IBL**: continue from `shader-3560-analysis.md`; the
   close peer blob 3559 is interchangeable.
3. **sun-shadow**: open
   `Scratch/shaders-extracted/ShadersFX/index/Shaders011/asm/Shaders011.2147.8fb709c2fdf0.dxbc.asm`
   and reverse-engineer.

## Cross-references

- Canonical: `Fallout4RE/Workspace/docs/d3dcompile-static-analysis.md`
- Negative-finding artifact:
  `Fallout4RE/Workspace/exports/cs-d3dcompile-call-sites.json`
- Corpus survey (revision banner added):
  `shaders/lighting/shader-corpus-survey.md`
- Per-shader analyses:
  - `shaders/lighting/shader-2122-analysis.md`
  - `shaders/lighting/shader-3147-analysis.md`
  - `shaders/lighting/shader-3560-analysis.md`
- Catalog plugin spec (Phase 2 deprecation + reframe):
  `Fallout4RE/Workspace/docs/shader-catalog-plugin-spec.md`
- Catalog schema (v2 migration plan in comments):
  `Fallout4RE/Workspace/schemas/runtime/shader-catalog.sqlite.schema.sql`

