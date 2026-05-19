# `shaders/lighting/` follow-ups

Persistent tracker for `// TODO: identify` markers and unresolved questions
surfaced by lighting-shader-reconstruction campaigns. New entries append
under their shader-blob header. Migrate, do not delete.

## Shaders011.2122 / `deferred_composite.hlsl`

> **SUPERSEDED 2026-05-18** by the d3dcompile-static-analysis byte-diff
> follow-up and the reconstruct-deferred-pipeline Target 1 campaign.
> The canonical composite blob is **3539** (`861504f6dcbe`), not 2122.
> Blob 2122 was a Phase A classifier false-positive (it's a per-light
> geometry pass, not a composite). See `shader-2122-analysis.md` for
> the original negative findings; the section below is preserved as
> a record of the false-positive sequence.
>
> The actively-tracked open items for the deferred composite are
> below under §`Shaders011.3539`.

Campaign: Fallout4RE 2026-05-18 (see `shader-2122-analysis.md` for full
context).

### Resolved this campaign

- Blob 2122 (sha1 `af996dd590c2…`, 105 insns) **misidentified** by the
  Phase-A classifier as `deferred-composite`. Actual binding pattern
  (per-vertex TEXCOORD/POSITION inputs, sun BRDF math against `cb0[2]`
  direction + `cb1[8].x` power, no albedo SRV) indicates a per-light
  deferred geometry pass, not a fullscreen composite. ASM materialised
  at `Fallout4RE/Scratch/shaders-extracted/ShadersFX/index/Shaders011/asm/Shaders011.2122.af996dd590c2.dxbc.asm`.
- Blob 556 (sha1 `3d7efefcfa9c…`, 45 insns) **also misidentified**.
  Actually a particle/decal shader (per-vertex inputs, `discard_nz` on
  distance fade, `pow(x, 1/2.2)` gamma decode on output alpha). ASM
  pre-existing in the same index folder.
- `DrawWorld::DeferredComposite` (REL::ID `{728427, 2318313, 2318313}`)
  is a **C++ orchestrator that dispatches 3 separate `RenderPassImmediately`
  calls plus 4 `BSShaderAccumulator::RenderGeometryGroup` renders** across
  4 render-target switchovers. There is no single "composite PS"; the
  composite is a multi-pass operation.
- The actual deferred-composite PS in the live RenderDoc capture
  `FO4_frame5407.rdc` is at **eid 45368**, sha1 prefix **`813c9acec23b`**,
  3172 bytes, 6 SRVs, 2 CBs, writes RT 172 (kMain, R11G11B10F). This PS
  fires immediately after the ambient/IBL pass (eid 45345) and is the
  first follow-up draw to RT 172.

### Open items

- [ ] **Locate the on-disk source of the composite PS sha1 `813c9acec23b…`**.
  Scan results so far: not in `Shaders011.fxp` (0 of 3939 DXBC blobs),
  not in `Fallout4.unpacked.exe` (0 of 924 embedded DXBC blobs). Next
  steps documented in `shader-2122-analysis.md` §"Finding 4":
  RenderDoc `Save Shader Bytecode` of eid 45368 + byte-for-byte search
  across FO4 install dir, or dynamic instrumentation of
  `ID3D11Device::CreatePixelShader` during the deferred-composite frame.
- [ ] **Map the 6 SRV slots of eid 45368's PS to `cs::engine::RenderTarget`
  enum entries** (analogous to the Phase C work done for shader 3560).
  Requires extending the existing dump-eid script to target eid 45368
  instead of 45345 and re-running against `FO4_frame5407.rdc`.
- [ ] **Disambiguate the 3 separate PS dispatches inside
  `DrawWorld::DeferredComposite`**. The C++ orchestrator issues
  `BSShaderManager::GetShader` + `BSRenderPass` + `RenderPassImmediately`
  three times (AE offsets +0x01F5, +0x0915, +0x09DE). Determine which
  one corresponds to eid 45368 by walking the orchestrator's full
  callsite-ordered dispatch and matching against the live capture's
  eid-ordered draw sequence (similar to the Render_PreUI anchor-walk
  done for the Deferred* triplet in commit `20e5fa7`). The other 2 PS
  dispatches are likely fog and a final HDR copy / motion-vector clear,
  but this needs evidence.
- [ ] **Identify what blob 2122 actually is.** Sun disc? Sky dome lit pass?
  Distant terrain BRDF? Per-light geometry pass for the directional sun?
  Cross-check against eid 45368-45623 in the live capture by matching
  blob 2122's PS sha against the captured PS shas in that range. If
  matched, rename the analysis target. If not, search the rest of the
  capture (all 5240 actions) for the sha.
- [ ] **Update the indexer / classifier heuristic** to exclude per-vertex
  inputs from the "deferred-composite" bucket. Current rule "PS, 3 SRVs,
  1 SV_Target out, 30-120 insns" produces false positives whenever a
  decal or per-light geometry shader matches the shape. The rule should
  require `input_signature == [{name: 'SV_POSITION', ...}]` (fullscreen
  quad shape) to be a composite candidate.
- [ ] **Document permutation flags after PS is located**. Per-runbook,
  diff two captures (outdoor + indoor night) for the composite PS to
  identify scene-dependent permutation bits. Requires a second RenderDoc
  capture; not budgeted this campaign.

### Status row

`shaders/lighting/README.md` updated this campaign:
`candidates-identified` → **`wip-blob-misidentified`** for
`deferred_composite.hlsl`. Reverts to `candidates-identified` once the
real PS source is located.

## Shaders011.3147 / `directional_sun_light.hlsl`

> **SUPERSEDED 2026-05-18 (partial)** by the d3dcompile-static-analysis
> byte-diff follow-up. The captured sun-shadow PS (eid 45401, sha
> `46b911cb8053`) was found to mnemonic-match corpus blob **2147**
> (`8fb709c2fdf0`), not 3147. Blob 3147 belongs to a 30+ peer cluster
> for the directional-sun-light role but does not match any captured
> PS in the reference scene. The sun-shadow reconstruction target is
> blob 2147, queued under §`Shaders011.2147` below.
>
> The 30-peer-cluster ambiguity for 3147 itself remains open: any
> future "which permutation does the engine actually load?" question
> still needs the runtime catalog plugin (WU3+WU4) to resolve.

Campaign: Fallout4RE 2026-05-18 (see `shader-3147-analysis.md` and
`shader-corpus-survey.md` for full context).

### Resolved this campaign

- **Three-step verification** completed against the live capture.
  Step 1 (input signature fullscreen-quad-only): PASS — `SV_POSITION`
  is the only used input; `POSITION:14` is declared but unused. Step 2
  (MRT to `o0`+`o1`): PASS. Step 3 (sha cross-check against live
  capture): **FAIL** — 3147's sha1 `a5e2f8a0985e…` is not the captured
  sun-shadow PS. The captured shadow draws (eids 45401-45623, 22
  dispatches) use sha `46b911cb8053…`, srv=1 cb=4 — a completely
  different shape, not present in the on-disk corpus.
- **30+ peer cluster identified**: querying the unified corpus for
  PSes matching 3147's exact shape (`stage=ps srv=6 out=2 cb=2
  position-only-input`) returns 30 candidates from `Shaders011.fxp`.
  3147 is the heaviest-instruction representative the Phase-A
  classifier picked; no live evidence singles it out as canonical.

### Open items

- [ ] **Identify which permutation the engine actually loads** for the
  outdoor-day capture's sun-shadow path. Three approaches:
  - Runtime instrumentation (`ID3D11Device::CreatePixelShader` hook
    via F4SE plugin); blocks on WU4 of the corpus-completeness work.
  - More RenderDoc captures across permutation axes (sun-on/off,
    IBL-on/off, cascade-count, indoor/outdoor) — out-of-budget here.
  - Extend the indexer to read the `.fxp` outer-framing technique bits
    so the 30-peer cluster maps onto Bethesda's compile flags directly.
- [ ] **Disambiguate the captured `46b911cb8053`** — is it a
  cascaded-shadow-only depth resolve, or does it carry per-light BRDF
  math? Disassembly attempt requires extracting the bytecode from the
  capture (RenderDoc "Save Shader Bytecode" UI action).
- [ ] **Update the Phase-A classifier** to surface peer-cluster size as
  a confidence signal. Currently the classifier picks "heaviest in
  cluster" with no record of cluster ambiguity. A cluster size of 30
  should be a yellow flag, not a green light.
- [ ] **Defer the 177-insn bilateral blur HLSL round-trip from 3560**
  per the prompt's "fold-in" item. Originally intended to pair with
  the 3147 reconstruction (same dxc iteration loop, same tooling);
  since 3147 reconstruction is itself deferred, the 3560 blur block
  remains WIP.

### Status row

`shaders/lighting/README.md` updated this campaign:
`candidates-identified` → **`wip-permutation-uncertain`** for
`directional_sun_light.hlsl`. Reverts (or advances) once a canonical
permutation is identified.

## Shader corpus completeness — campaign findings

Campaign: Fallout4RE 2026-05-18 (see `shader-corpus-survey.md`).

### Resolved this campaign

- **WU1 complete**: unified `shader-corpus.sqlite` covers
  `Shaders011.fxp` + PE-embedded blobs across OG/NG/AE. 3,242 unique
  sha1 shaders across 4 indexes (3,939 + 924×3 = 6,711 rows, dedup
  to 3,242 via cross-runtime fxp/exe sharing).
- **WU-B Path A complete (definitive negative)**: deferred-composite
  PS sha `813c9acec23b…` does not exist on disk anywhere in the FO4
  install or the cross-runtime exes (6,545 DXBC blobs scanned across
  9 unique DXBC-bearing files, 0 matches).
- **Generalised corpus gap finding**: of 6 PS sha1 prefixes captured
  in the deferred chain (eids 45345-45718), 3 are unknown to the
  corpus, and the 3 unknowns are precisely the deferred-stage PSes
  (ambient/IBL, composite, sun-shadow). The 3 knowns are all
  ~1 KiB decal/blood-splatter draws coincidentally writing to RT 172.
- **59-archive `.ba2` audit**: only `Fallout4 - Shaders.ba2` (already
  indexed) contains any shader content; the other 58 are
  textures/meshes/voices/animations with zero shader content.
- **Tools shipped** (Fallout4RE-side):
  `audit-ba2-archives.py`, `index-pe-embedded-shaders.py`,
  `build-shader-corpus.py`, `query-shader-corpus.py`,
  `find-shader-by-sha.py`.

### Open items

- [ ] **WU2 full coverage** — 6-capture survey across the
  outdoor-day / outdoor-night / indoor / combat / PA-HUD / weather
  permutation axes. Current partial survey used the 4 existing
  captures (1 deferred-walk used for the report). Refines the
  "L unknown" count but does not change the qualitative answer.
- [ ] **WU-B Path B** — dynamic
  `ID3D11Device::CreatePixelShader` instrumentation via a F4SE
  plugin. The only remaining way to locate the actual bytecode
  source for the runtime-generated deferred PSes (composite,
  ambient/IBL, sun-shadow). Out of autopilot scope.
- [ ] **WU3** — per-`BSShader` subclass `LoadShaders` enumeration via
  IDA Hex-Rays. Gated on explicit user OK per prompt's phasing.
- [ ] **WU4** — runtime catalog F4SE plugin
  (`Data/F4SE/Plugins/FO4CommunityShaders/shader-catalog.sqlite`).
  The "permanent solution" per the prompt. **Required** (not
  optional) given `L > 0`. Gated on explicit user OK.
- [ ] **Phase-A classifier tightening** — require
  `input_has_position_only = 1` for any composite / ambient /
  directional-sun bucket entry. The corpus DB now stores this
  column; the classifier should consume it. The 2122 (per-vertex
  TEXCOORD) and 3147 (POSITION:14 unused) false positives both
  would have been caught by this rule combined with peer-cluster
  size reporting.

### Status

The corpus-completeness work is **not** a WIP marker on a specific
shader file. It is a documented finding about the shape of Bethesda's
shader pipeline. The actionable next-step is **WU4** when the user
greenlights it.

## Shaders011.3560 / `ambient_ibl_pass.hlsl`

Carried over from `shaders/lighting/shader-3560-analysis.md`. Structural
analysis (Phase A/B/C from the 2026-05-07/08 campaigns, sibling-repo
commits `c9978ce`, `dfbdd6f`, `6b93c09`) is complete. HLSL round-trip
is gated on the items below.

### Open items

- [ ] **177-insn bilateral SSSS-style blur block** (ASM lines 61-238).
  Requires multi-iteration HLSL → DXC → diff loop that exceeds the
  runbook's single-pass budget. To resolve: pair with the
  `Shaders011.3147` campaign (same iteration problem, same tooling)
  and ship as a single HLSL with the blur block named explicitly.
- [ ] **CB12 [30..46] semantic gap.** CB byte contents are dumped but
  the named struct mapping is pending. To resolve: cross-reference the
  dispatch-site CB-update C++ via the `DrawWorld::DeferredLightsImpl`
  REL::ID `{1108521, 2318312, 2318312}` function body in
  `Fallout4RE/exports/cs-render-subsystem-ids.json`. IDA Pro 9.3 +
  Hex-Rays is now registered in
  `Fallout4RE/Workspace/TOOLS.local.json` and is the preferred tool
  for this cross-read.
- [ ] **Medium-confidence SRV bindings** (t2 / t3 gbuffer aux,
  t5+t11 ambient diffuse pair, t6+t10+t12 screen-space ambient HDR
  scratch). RenderDoc capture `FO4_frame5407.rdc` event-id 45345 has
  the data; the per-slot mapping to canonical RT indices is the gap.
  To resolve: a second RenderDoc capture in a contrasting permutation
  (indoor night vs outdoor day) to lock the bindings.


## Shaders011.3539 / `deferred_composite.hlsl` (reconstructed-roundtrip-wip)

Campaign: Fallout4RE 2026-05-18 reconstruct-deferred-pipeline Target 1.
HLSL reconstruction shipped as honest WIP after one-pass asm-to-source
transcription. See `../deferred_composite.hlsl` header for the full
round-trip notes; in short: resource bindings exact-match, sample count
exact-match (6/6), signature exact-match, but instruction count is 108
vs original 90 (+20%) - outside the 5% threshold the prompt set, within
runbook §230-232 WIP territory.

### Resolved this campaign

- Canonical blob identified as 3539 (`861504f6dcbe`) by mnemonic-stream
  equivalence with captured runtime PS at eid 45368 (`813c9acec23b`).
- Faithful asm-to-HLSL transcription of all 90 instructions. Structural
  fidelity high: depth-based matrix select, material-id gate on
  `{skin, hair}`, view-space position reconstruction, fog-color
  4-corner lerp with intensity ramp, sun-direction lighting, grayscale-
  saturation tonemap, all preserved.
- Identified the FO4 composite as fundamentally different from the
  Skyrim CS `ISLightingComposite` / `ISSAOComposite` analogs (FO4
  does its own view-space position reconstruction + sun lighting in
  this PS rather than reading pre-accumulated DirDiffuse / DirSpecular
  buffers).

### Open items

- [ ] **CB12 field semantic names**. The reconstruction uses
  `cb12_idx<N>_<inferred-role>` placeholders for indices 14, 35, 41,
  42, 43, 44, 45, 46 plus the two 4x4 reprojection matrices at 20..23
  and 24..27. The remaining indices [0..13, 15..19, 28..34, 36..40] are
  unused by this PS but the dispatch-site C++ should populate them.
  To resolve: IDA Hex-Rays on `DrawWorld::DeferredComposite` AE RVA
  `0x021F0790` body, walk the `ID3D11DeviceContext::PSSetConstantBuffers`
  + the per-frame CB12 update site.
- [ ] **CB2 field semantic names**. Three vec4s; placeholders use
  `cb2_idx0_screen_uv_scale`, `cb2_idx1_sun_dir_and_intensity`,
  `cb2_idx2_sun_color_and_spec_power`. The `.w` channels of each
  carry distinct meanings (UV scale, intensity, spec power respectively)
  but the actual struct layout needs cross-read.
- [ ] **Texture RT-index mapping**. The rdoc capture eid 45368 binds
  RT 250 / 256 / 253 / 389 / depth-183 / 395 - all of which are above
  the highest committed `cs::engine::RenderTarget` enum value
  (kSSAOFinalSwap2 = 47). They appear to be dynamic per-frame scratch
  RTs allocated by RenderTargetManager. To resolve: walk RenderTarget-
  Manager's allocation log via IDA, or extend the sibling-repo enum
  with the dynamic scratch indices.
- [ ] **Round-trip tightening** (+20% -> <5%). Try alternative HLSL
  patterns for: the matrix select (per-row movc instead of float4x4
  assign), the secondary-color-vs-grayscale tonemap blend (compress
  to fewer mads), and the fog intensity threshold branch (movc
  instead of if/else). Each iteration: edit -> fxc /T ps_5_0 /O3 ->
  diff against original.asm via existing
  `tools/commands/diff-runtime-vs-corpus.py`-style mnemonic check.
- [ ] **Permutation diff** vs a second RenderDoc capture (indoor cell
  + interior night). Out-of-budget this campaign; deferred. The 4
  existing captures may suffice; re-run
  `tools/renderdoc-scripts/dump-deferred-ps-bytecode.py` against
  `FO4_frame9483.rdc` to see if the same eid pattern yields a
  different captured-PS sha (which would surface the permutation).
- [ ] **Skyrim CS analog cite + comparison appendix**. The HLSL header
  mentions `ISLightingComposite.hlsl` as the closest analog; a
  side-by-side compare in this followups section would help future
  reconstructors understand which math ports and which doesn't.

## Shaders011.2147 / `directional_sun_light.hlsl` (RECONSTRUCTION QUEUED)

Campaign queued: Fallout4RE 2026-05-18 reconstruct-deferred-pipeline
Target 3. Canonical sun-shadow blob per 2026-05-18 mnemonic-diff: 2147
(`8fb709c2fdf0`). Captured runtime PS at eid 45401 in
`FO4_frame5407.rdc` (sha `46b911cb8053`) is exact-mnemonic match
(62 / 62 insns, 1 / 1 samples). Asm available at
`Fallout4RE/Scratch/shaders-extracted/ShadersFX/index/Shaders011/asm/Shaders011.2147.8fb709c2fdf0.dxbc.asm`.

### Open items

- [ ] Faithful asm-to-HLSL reconstruction (62 insns, 1 SRV =
  `kShadowMap` cascade, 4 CBs, 2 SV_Target outputs). The 22-dispatch
  pattern at eids 45401-45623 suggests per-cascade or per-light
  iteration; confirm via dispatch-site C++.
- [ ] PCF kernel identification (3x3? 5x5? rotated Poisson?).
- [ ] CB layout cross-read for 4 CBs (per-pass, per-light, per-frame,
  per-material - likely).
- [ ] fxc round-trip within 5%.
- [ ] Sibling README status: `wip-permutation-uncertain` -> `reconstructed`.
- [ ] Append-only `shader-3147-analysis.md` with a `Superseded by
  2147` note (keep the negative-findings record).

## Shaders011.3559 / `ambient_ibl_pass.hlsl` (RECONSTRUCTION QUEUED)

Campaign queued: Fallout4RE 2026-05-18 reconstruct-deferred-pipeline
Target 2 (the big one - 263 insns, 14 SRVs, 3 CBs). Canonical
ambient/IBL blob per mnemonic-diff: 3559 (`7460585eaf76`); the
already-analyzed 3560 (`2b6e36c08aca`) is the slightly-larger sibling
(265 insns, 321 if counting the bilateral-blur expansion). Pre-existing
structural work in `shader-3560-analysis.md` (Phase A/B/C, 14 SRVs
mapped, AO-application boundary at line 264, kSSAO write timeline
identified, CB12 byte content dumped) carries over.

### Open items

- [ ] HLSL reconstruction including the 177-insn bilateral SSSS-style
  blur block that blocked the May 7-8 campaign. With Target 1's
  workflow proven (this campaign's outcome) the iteration loop is
  better-understood; tackle the blur block by referencing Skyrim CS's
  SSSS reconstruction at
  `.local/skyrim-community-shaders-dev/package/Shaders/SubsurfaceScattering/`
  (search for the SSSS files first - they may not be at that exact
  path).
- [ ] Close the CB12 [30..46] semantic gap via IDA Hex-Rays on
  `DrawWorld::DeferredLightsImpl` AE RVA `0x021ed4c0` body. Many
  of the CB12 indices used here OVERLAP with the indices used by
  the composite PS (3539 uses indices 14, 35, 41..46 from CB12);
  cross-resolution may share results.
- [ ] Medium-confidence SRV bindings (t2/t3/t5/t11/t6/t10/t12) - close
  via dispatch-site C++ now that IDA is registered.
- [ ] fxc round-trip within ~10% (looser because shader is larger).
- [ ] Append closure to `shader-3560-analysis.md`.

