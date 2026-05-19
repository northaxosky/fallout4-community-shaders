# `shaders/lighting/` follow-ups

Persistent tracker for `// TODO: identify` markers and unresolved
questions surfaced by the lighting-shader reconstruction work. New
entries append under their shader-blob header. Migrate, don't delete.

## Shaders011.2122 / `deferred_composite.hlsl`

> **SUPERSEDED 2026-05-18** by the byte-diff follow-up. The canonical
> composite blob is **3539** (`861504f6dcbe`), not 2122. Blob 2122 was
> a shape-based classifier false-positive (it's a per-light geometry
> pass, not a composite). The section below is preserved as a record
> of the false-positive sequence.
>
> The actively-tracked open items for the deferred composite are
> below under §`Shaders011.3539`.

### Resolved

- Blob 2122 (sha1 `af996dd590c2...`, 105 insns) **misidentified** by
  the initial classifier as `deferred-composite`. Actual binding
  pattern (per-vertex TEXCOORD/POSITION inputs, sun BRDF math against
  `cb0[2]` direction + `cb1[8].x` power, no albedo SRV) indicates a
  per-light deferred geometry pass, not a fullscreen composite.
- Blob 556 (sha1 `3d7efefcfa9c...`, 45 insns) **also misidentified**.
  Actually a particle/decal shader (per-vertex inputs, `discard_nz`
  on distance fade, `pow(x, 1/2.2)` gamma decode on output alpha).
- `DrawWorld::DeferredComposite` (REL::ID `{728427, 2318313, 2318313}`)
  is a **C++ orchestrator that dispatches 3 separate
  `RenderPassImmediately` calls plus 4
  `BSShaderAccumulator::RenderGeometryGroup` renders** across 4
  render-target switchovers. There is no single "composite PS"; the
  composite is a multi-pass operation.
- The actual deferred-composite PS in the live RenderDoc capture
  `FO4_frame5407.rdc` is at **eid 45368**, sha1 prefix
  **`813c9acec23b`**, 3172 bytes, 6 SRVs, 2 CBs, writes RT 172 (kMain,
  R11G11B10F). This PS fires immediately after the ambient/IBL pass
  (eid 45345) and is the first follow-up draw to RT 172.

### Open items

- [ ] **Map the 6 SRV slots of eid 45368's PS** to
  `cs::engine::RenderTarget` enum entries (analogous to the SRV-map
  work done for shader 3560). Requires extending the existing
  dump-eid script to target eid 45368 instead of 45345 and re-running
  against `FO4_frame5407.rdc`.
- [ ] **Disambiguate the 3 separate PS dispatches inside
  `DrawWorld::DeferredComposite`**. The C++ orchestrator issues
  `BSShaderManager::GetShader` + `BSRenderPass` +
  `RenderPassImmediately` three times (AE offsets +0x01F5, +0x0915,
  +0x09DE). Determine which one corresponds to eid 45368 by walking
  the orchestrator's full callsite-ordered dispatch and matching
  against the live capture's eid-ordered draw sequence. The other 2
  PS dispatches are likely fog and a final HDR copy /
  motion-vector clear, but this needs evidence.
- [ ] **Identify what blob 2122 actually is.** Sun disc? Sky dome lit
  pass? Distant terrain BRDF? Per-light geometry pass for the
  directional sun? Cross-check against eid 45368-45623 in the live
  capture by matching blob 2122's PS sha against the captured PS shas
  in that range.
- [ ] **Update the shape-based classifier** to exclude per-vertex
  inputs from the "deferred-composite" bucket. Current rule "PS, 3
  SRVs, 1 SV_Target out, 30-120 insns" produces false positives
  whenever a decal or per-light geometry shader matches the shape.
  The rule should require `input_signature == [{name: 'SV_POSITION',
  ...}]` (fullscreen quad shape) to be a composite candidate.
- [ ] **Document permutation flags after PS is located**. Diff two
  captures (outdoor + indoor night) for the composite PS to identify
  scene-dependent permutation bits. Requires a second RenderDoc
  capture.

## Shaders011.3147 / `directional_sun_light.hlsl`

> **SUPERSEDED 2026-05-18 (partial)** by the byte-diff follow-up. The
> captured sun-shadow PS (eid 45401, sha `46b911cb8053`) was found to
> mnemonic-match corpus blob **2147** (`8fb709c2fdf0`), not 3147.
> Blob 3147 belongs to a 30+ peer cluster for the directional-sun-
> light role but does not match any captured PS in the reference scene.
>
> The 30-peer-cluster ambiguity for 3147 itself remains open: any
> future "which permutation does the engine actually load?" question
> still needs per-`BSShader`-subclass enumeration to resolve.

### Resolved

- **Three-step verification** completed against the live capture.
  Input signature fullscreen-quad-only: PASS - `SV_POSITION` is the
  only used input; `POSITION:14` is declared but unused. MRT to
  `o0`+`o1`: PASS. Sha cross-check against live capture: **FAIL** -
  3147's sha1 `a5e2f8a0985e...` is not the captured sun-shadow PS.
  The captured shadow draws (eids 45401-45623, 22 dispatches) use
  sha `46b911cb8053...`, srv=1 cb=4 - a completely different shape,
  not present in the on-disk corpus under blob 3147.
- **30+ peer cluster identified**: querying the unified corpus for
  PSes matching 3147's exact shape (`stage=ps srv=6 out=2 cb=2
  position-only-input`) returns 30 candidates from `Shaders011.fxp`.
  3147 is the heaviest-instruction representative the initial
  classifier picked; no live evidence singles it out as canonical.

### Open items

- [ ] **Identify which permutation the engine actually loads** for
  the outdoor-day capture's sun-shadow path. Three approaches:
  - Runtime instrumentation
    (`ID3D11Device::CreatePixelShader` hook via F4SE plugin); blocks
    on the catalog import workflow.
  - More RenderDoc captures across permutation axes (sun-on/off,
    IBL-on/off, cascade-count, indoor/outdoor).
  - Extend the indexer to read the `.fxp` outer-framing technique
    bits so the 30-peer cluster maps onto Bethesda's compile flags
    directly.
- [ ] **Disambiguate the captured `46b911cb8053`** - is it a
  cascaded-shadow-only depth resolve, or does it carry per-light BRDF
  math? Disassembly requires extracting the bytecode from the capture
  (RenderDoc "Save Shader Bytecode" UI action).
- [ ] **Update the shape-based classifier** to surface peer-cluster
  size as a confidence signal. Currently the classifier picks
  "heaviest in cluster" with no record of cluster ambiguity. A cluster
  size of 30 should be a yellow flag, not a green light.
- [ ] **Defer the 177-insn bilateral blur HLSL round-trip from 3560**.
  Originally intended to pair with the 3147 reconstruction (same dxc
  iteration loop, same tooling); since 3147 reconstruction is itself
  deferred, the 3560 blur block remains WIP.

## Shader corpus completeness

### Resolved

- **On-disk corpus scan complete**: unified `shader-corpus.sqlite`
  covers `Shaders011.fxp` + PE-embedded blobs across OG/NG/AE. 3,242
  unique sha1 shaders across 4 indexes (3,939 + 924×3 = 6,711 rows,
  dedup to 3,242 via cross-runtime fxp/exe sharing).
- **Definitive negative**: deferred-composite PS sha
  `813c9acec23b...` does not exist on disk anywhere in the FO4
  install or the cross-runtime exes (6,545 DXBC blobs scanned across
  9 unique DXBC-bearing files, 0 matches).
- **Generalised corpus gap finding**: of 6 PS sha1 prefixes captured
  in the deferred chain (eids 45345-45718), 3 are unknown to the
  corpus, and the 3 unknowns are precisely the deferred-stage PSes
  (ambient/IBL, composite, sun-shadow). The 3 knowns are all ~1 KiB
  decal/blood-splatter draws coincidentally writing to RT 172. The
  byte-diff follow-up later resolved this: the "missing" PSes are
  re-encoded versions of corpus blobs (same mnemonic stream,
  different bytecode encoding).
- **59-archive `.ba2` audit**: only `Fallout4 - Shaders.ba2` (already
  indexed) contains any shader content; the other 58 are
  textures/meshes/voices/animations with zero shader content.

### Open items

- [ ] **Live-capture cross-reference: full coverage**. 6-capture
  survey across the outdoor-day / outdoor-night / indoor / combat /
  PA-HUD / weather permutation axes. The current partial survey used
  the 4 existing captures.
- [ ] **Dynamic `ID3D11Device::CreatePixelShader` instrumentation**
  via the F4SE plugin. The remaining way to locate the actual
  bytecode source for the runtime-generated deferred PSes (composite,
  ambient/IBL, sun-shadow).
- [ ] **Per-`BSShader`-subclass enumeration** of `LoadShaders` via
  IDA Hex-Rays.
- [ ] **Catalog import** workflow to fold per-session runtime
  catalogs into a long-lived analysis DB.
- [ ] **Classifier tightening** - require `input_has_position_only =
  1` for any composite / ambient / directional-sun bucket entry. The
  2122 (per-vertex TEXCOORD) and 3147 (POSITION:14 unused) false
  positives both would have been caught by this rule combined with
  peer-cluster size reporting.

## Shaders011.3560 / `ambient_ibl_pass.hlsl`

Carried over from `shaders/lighting/shader-3560-analysis.md`.
Structural analysis is complete. HLSL round-trip is gated on the
items below.

### Open items

- [ ] **177-insn bilateral SSSS-style blur block** (ASM lines 61-238).
  Requires multi-iteration HLSL -> DXC -> diff loop. To resolve: pair
  with the `Shaders011.3147` work (same iteration problem, same
  tooling) and ship as a single HLSL with the blur block named
  explicitly.
- [ ] **CB12 [30..46] semantic gap.** CB byte contents are dumped but
  the named struct mapping is pending. To resolve: cross-reference
  the dispatch-site CB-update C++ via the `DrawWorld::DeferredLightsImpl`
  REL::ID `{1108521, 2318312, 2318312}` function body.
- [ ] **Medium-confidence SRV bindings** (t2 / t3 gbuffer aux, t5+t11
  ambient diffuse pair, t6+t10+t12 screen-space ambient HDR scratch).
  RenderDoc capture `FO4_frame5407.rdc` event-id 45345 has the data;
  the per-slot mapping to canonical RT indices is the gap. To
  resolve: a second RenderDoc capture in a contrasting permutation
  (indoor night vs outdoor day) to lock the bindings.


## Shaders011.3539 / `deferred_composite.hlsl` (reconstructed-roundtrip-wip)

HLSL reconstruction shipped after one-pass asm-to-source
transcription. See `../deferred_composite.hlsl` header for the full
round-trip notes; in short: resource bindings exact-match, sample
count exact-match (6/6), signature exact-match, but instruction count
is 108 vs original 90 (+20%) - structural fidelity verified,
instruction-count delta documented.

### Resolved

- Canonical blob identified as 3539 (`861504f6dcbe`) by mnemonic-
  stream equivalence with captured runtime PS at eid 45368
  (`813c9acec23b`).
- Faithful asm-to-HLSL transcription of all 90 instructions.
  Structural fidelity high: depth-based matrix select, material-id
  gate on `{skin, hair}`, view-space position reconstruction, fog-
  color 4-corner lerp with intensity ramp, sun-direction lighting,
  grayscale-saturation tonemap - all preserved.
- Identified the FO4 composite as fundamentally different from the
  Skyrim CS `ISLightingComposite` / `ISSAOComposite` analogs (FO4
  does its own view-space position reconstruction + sun lighting in
  this PS rather than reading pre-accumulated DirDiffuse /
  DirSpecular buffers).

### Open items

- [ ] **CB12 field semantic names**. The reconstruction uses
  `cb12_idx<N>_<inferred-role>` placeholders for indices 14, 35, 41,
  42, 43, 44, 45, 46 plus the two 4x4 reprojection matrices at 20..23
  and 24..27. The remaining indices [0..13, 15..19, 28..34, 36..40]
  are unused by this PS but the dispatch-site C++ should populate
  them. To resolve: IDA Hex-Rays on `DrawWorld::DeferredComposite` AE
  RVA `0x021F0790` body, walk the
  `ID3D11DeviceContext::PSSetConstantBuffers` + the per-frame CB12
  update site.
- [ ] **CB2 field semantic names**. Three vec4s; placeholders use
  `cb2_idx0_screen_uv_scale`, `cb2_idx1_sun_dir_and_intensity`,
  `cb2_idx2_sun_color_and_spec_power`. The `.w` channels of each
  carry distinct meanings (UV scale, intensity, spec power
  respectively) but the actual struct layout needs cross-read.
- [ ] **Texture RT-index mapping**. The rdoc capture eid 45368 binds
  RT 250 / 256 / 253 / 389 / depth-183 / 395 - all of which are above
  the highest committed `cs::engine::RenderTarget` enum value
  (kSSAOFinalSwap2 = 47). They appear to be dynamic per-frame scratch
  RTs allocated by RenderTargetManager. To resolve: walk
  RenderTargetManager's allocation log via IDA, or extend the
  RenderTarget enum with the dynamic scratch indices.
- [ ] **Round-trip tightening** (+20% -> <5%). Try alternative HLSL
  patterns for: the matrix select (per-row movc instead of float4x4
  assign), the secondary-color-vs-grayscale tonemap blend (compress
  to fewer mads), and the fog intensity threshold branch (movc
  instead of if/else). Each iteration: edit -> fxc /T ps_5_0 /O3 ->
  diff against original.asm.
- [ ] **Permutation diff** vs a second RenderDoc capture (indoor cell
  + interior night). The 4 existing captures may suffice; re-run the
  dump tooling against `FO4_frame9483.rdc` to see if the same eid
  pattern yields a different captured-PS sha (which would surface the
  permutation).
- [ ] **Skyrim CS analog cite + comparison appendix**. The HLSL
  header mentions `ISLightingComposite.hlsl` as the closest analog; a
  side-by-side compare would help future reconstructors understand
  which math ports and which doesn't.

## Shaders011.2147 / `directional_sun_light.hlsl` (reconstructed-roundtrip-wip-role-tbd)

HLSL reconstruction shipped. fxc round-trip: 83 insns vs original 62
(+33.9%). Resource bindings + sample count (1/1) + signature all
exact-match.

### Resolved

- Canonical blob 2147 (`8fb709c2fdf0`) confirmed as mnemonic-stream
  exact match to captured runtime PS at eid 45401 (`46b911cb8053`)
  by the 2026-05-18 byte-diff work.
- Faithful asm-to-HLSL transcription of all 62 instructions: depth-
  based matrix select (SHARED with composite blob 3539 - same CB12
  rows 20..27), view-space position reconstruction, back-projected
  view ray, dot product against sun direction (cb2[4].xyz), two
  smoothstep distance fades, color lerp output.
- Confirmed `out=1` SV_Target (initial classifier had it as `out=2`,
  which was wrong); rdoc capture's `out_rt = 172` is a single output.
  Reconstruction matches the asm.

### Role discrepancy

The original label of "directional sun light / sun shadow PS" was
based on the captured 22-dispatch pattern at eids 45401-45623. The
asm does NOT match a shadow-mapping shape:

- 1 SV_Target output (not 2 MRT).
- 1 SRV sampler with `mode_default` (not `mode_comparison`; no
  hardware PCF).
- No `SampleCmp` operation (no shadow comparison).
- Math: view-ray-vs-sun-direction dot + distance smoothstep + sky-
  color lerp. Textbook FO4 god-rays / atmospheric-scattering shape.

Role: per-light god-rays / volumetric-scattering / sky-sampling PS.
The 22-dispatch pattern at the captured eids corresponds to per-
light-volume iteration (one dispatch per visible light source with
god-rays enabled) rather than per-cascade shadow.

- [ ] **Confirm actual role via IDA Hex-Rays** on the dispatch site
  C++. The dispatch is inside `DrawWorld::DeferredLightsImpl`
  (REL::ID `{1108521, 2318312, 2318312}`, AE RVA `0x021ed4c0`).
  Walk the function body to find which BSShader subclass owns the
  technique at this dispatch site.
- [ ] **Rename the file** if the god-rays interpretation is
  confirmed. (Done: renamed to `vls_slice_scatter.hlsl`.)

### Open items

- [ ] **CB field semantic names** for CB0[0], CB1[0/1/10/12/13],
  CB2[4]. Currently placeholders (`cb<N>_idx<M>_*`). IDA Hex-Rays
  on the dispatch site C++ should reveal struct names.
- [ ] **t7 binding semantics**. Likely the same main depth gbuffer
  as the composite uses, but the rdoc walk recorded eid 45401's
  single SRV as depth target 183 (D24S8) - consistent with the
  composite's depth source. Confirm.
- [ ] **Round-trip tightening** (+33.9% -> <5%). The matrix-select
  pattern probably contributes most of the surplus (same problem as
  the composite); rework to per-row movc or inline dp4s with
  conditional select rather than copying a `float4x4`.
- [ ] **Per-light parameter variation**. The 22 dispatches at eids
  45401-45623 use different cb1/cb2 contents per dispatch (per-light
  parameters). Verify by extracting CB byte contents at 2-3 of those
  eids and diffing - if all 22 use identical CBs, it's not per-light.

## Shaders011.3559 / `ambient_ibl_pass.hlsl` (RECONSTRUCTED 2026-05-19, roundtrip +1.5%)

HLSL reconstruction shipped with round-trip +1.5% vs original (within
the ±10% threshold for this larger shader). Resource bindings +
signature exact-match; sample count 41 vs 44 (3 short due to a
missing +1.28 ring tap in the SSSS blur kernel, documented below).

### Resolved

- Canonical blob 3559 (`7460585eaf76`) confirmed: 14 SRVs, 14 samplers,
  3 CBs, fullscreen-quad input, single SV_Target to RT 58 =
  kDiffuseBuffer.
- Full 263-instruction reconstruction including the 9-tap SSSS
  bilateral blur block (insns 80-251) that previously blocked the
  May 7-8 work. Blur is implemented as a [unroll]'d loop over a
  static kernel array with Christensen-Burley per-RGB weights (red
  diffuses farthest, blue least - correct subsurface-scattering
  wavelength absorption).
- SSGI indirect-lighting AO-application boundary CONFIRMED: t9
  (kSSAO) single multiply on the combined ambient+IBL term at insns
  261-262, AFTER all cubemap reflection + bilateral blur + ambient
  accumulation, BEFORE downstream fog blending in the composite
  (blob 3539). Direct light is NEVER multiplied by AO via this path.
- CB12[20..27] shared deferred-pipeline reprojection matrix pattern
  CONFIRMED. Same pattern as composite (3539) + VLS slice scatter
  (2147); these matrices are global per-frame infrastructure reused
  across the deferred pipeline.
- shader-3560-analysis.md SRV map carries over (3559 is the slightly-
  smaller sibling of 3560 with same structure); 14 SRV roles
  identified at high confidence (kGbufferNormal=t1, depth=t7, IBL
  cube array=t8, kSSAO=t9, kMainPreAlpha=t14) + medium confidence
  (kGbufferMaterial=t2, shading data=t3, ambient pair=t5+t11,
  bilateral source=t10, depth ref=t15, probes=t6+t12, skin aux=t4).

### Round-trip result (fxc /T ps_5_0 /O3 /Ni)

| Metric | Original | Reconstructed | Status |
|---|---:|---:|---|
| Resource bindings | 14 SRVs + 14 samplers + 3 CBs at exact slots | identical | EXACT MATCH |
| Signature | SV_POSITION-only input + single SV_Target out | identical | EXACT MATCH |
| Instruction count | 265 | 269 | **+1.5%** (within ±10%) |
| Sample count | 44 | 41 | -3 (see open item below) |

### Open items

- [ ] **Add the missing +1.28 ring tap** to the SSSS_BLUR_OFFSETS
  table to close the sample-count gap (currently 41 vs 44 = 3 short).
  Trivial fix: append (1.28, 1.28) entry to the offsets array + a
  matching weight to SSSS_TAP_WEIGHTS (likely (0.019283, 0.002820,
  0.000842) by symmetry with the -1.28 tap). Recompile + diff.
- [ ] **CB12 field semantics beyond [12..14, 20..27, 30]**. CB12 has
  31 vec4s declared; most are unused by this PS but the dispatch site
  C++ populates them - cross-read via IDA Hex-Rays on the
  `DrawWorld::DeferredLightsImpl` body (AE RVA 0x021ed4c0). Many
  indices likely overlap with the composite shader (3539) which also
  uses CB12.
- [ ] **CB0[3] and CB2[6] field semantics**. Placeholders use
  `cb<N>_idx<M>_*` (no-speculation rule).
- [ ] **Texture RT-index mapping** for t4, t6, t10, t12, t15. The
  rdoc capture eid 45345 SRV bindings reference RT indices above the
  highest committed `cs::engine::RenderTarget` enum value (47); they
  appear to be dynamic RenderTargetManager scratch RTs. IDA cross-
  read needed.
- [ ] **Find the separable-blur PERPENDICULAR pass**. This PS does
  one axis of the separable Gaussian. Search candidates: blobs near
  3559 (3557-3561 range) with same shape (14 SRVs, 263+/- insns).
- [ ] **Permutation diff** against indoor / night RenderDoc capture.
  `FO4_frame9483.rdc` may surface different captured PS shas at the
  ambient/IBL eid range.
- [ ] **Skyrim CS SSSS analog comparison**. Skyrim CS's
  `package/Shaders/SubsurfaceScattering/` (if present) has a
  separable SSSS that would be worth diffing against this FO4 10-tap
  kernel.

## find-actual-sun-light-ps (CLOSED 2026-05-19, Outcome A)

Closed with Outcome A: the actual directional sun-light deferred PS
was located in the on-disk corpus.

The original `directional_sun_light.hlsl` reconstruction target
turned out to be the FO4 VLS slice scatter PS (blob 2147 =
VLSSliceScatterInterp), not a directional sun-light deferred PS. The
`shaders/lighting/` directory now has a proper `sun_light_deferred.hlsl`
(blob 3295) covering the deferred-lighting demonstrative artifact.

### Resolved

- Outcome A confirmed: separate PS, in the corpus, dispatched by
  `DrawWorld::AccumulateSunShadowLightImpl` (REL::IDs
  `{OG=259940, NG=2318296, AE=2318296}`, all confirmed cross-runtime).
- Host C++ chain identified: `BSDFLightShader` (NOT a separate
  `BSDFDirectionalLightShader` subclass - the sun light is a
  TECHNIQUE PERMUTATION of the same class that handles point/spot
  lights).
- 5-peer cluster identified: blob 3295 is the strongest candidate by
  all metrics; alternatives: 3234, 3250, 3268, 3182.
- Pre-eid-45345 PS inventory complete: 7 distinct MRT PSes in the
  deferred-lighting window before ambient/IBL. EID 44513 is the sun-
  light; EIDs 44848-45252 are the cascade-shadow-build chain (46
  dispatches building RT 205+215 -> RT 408 which the sun-light PS
  reads as Texture2DArray).

## Shaders011.2147 / vls_slice_scatter.hlsl (CLOSED 2026-05-19)

Previously tracked under §`Shaders011.2147` with role-TBD status.

### Resolved

- **Role confirmed**: per-slice scatter pixel shader in FO4's
  Volumetric Light Scattering (VLS) subsystem. Confirmed via PDB
  symbol walk + math-shape corroboration.
- **BSShader subclass identified** (leading candidate):
  BSImagespaceShaderVLSSliceScatterInterp. Alternative candidates (5
  other VLSSlice* siblings) remain on the table; locking to 100%
  certainty defers to per-BSShader-subclass catalog enrichment.
- **Host effect identified**: ImageSpaceEffectVLSLight::Render
  (per-light VLS path), with NVGodrays::RenderVolume(BSShadowLight*,
  int) as the NVIDIA helper. 22-dispatch pattern at eids 45401-45623
  is N slices × M shadow-lights.
- **Cross-runtime verified**: OG + AE both carry the full VLS symbol
  set; NG PDB is stripped to 3 VLS-family publics but the
  architectural finding generalizes (binary layout is consistent
  across runtimes).
- **Sibling file renamed**: `directional_sun_light.hlsl` ->
  `vls_slice_scatter.hlsl`. Header rewritten to drop the
  "sun-shadow" framing and document the actual VLS context.

### Remaining (not blockers)

- [ ] Lock the exact BSShader subclass (one of 6 VLS-family
  candidates) to 100% certainty. Defers to per-BSShader-subclass
  catalog enrichment OR per-subclass SetupTechnique IDA Hex-Rays
  cross-read.
- [ ] CB field semantic names (still placeholders; would close via
  IDA Hex-Rays on ImageSpaceEffectVLSLight::Setup).
- [ ] Round-trip tightening (+33.9% -> <5%); same matrix-select
  pattern issue as the composite reconstruction.
- [ ] Per-dispatch parameter variation across the 22 dispatches
  (extract CB byte contents at 2-3 sample eids and diff).

## Shaders011.3295 / `sun_light_deferred.hlsl` (RECONSTRUCTED 2026-05-19, roundtrip -8.8%)

Closes the deferred-pipeline reference set - `shaders/lighting/` now
has 4 reconstructed reference HLSL files covering the complete
deferred-lighting pipeline: deferred composite + ambient/IBL + VLS
slice scatter + sun-light deferred.

Canonical directional sun-light deferred PS located as **corpus blob
3295** (`50e2618e8d1a`), strongest candidate in a 5-peer cluster of
similar-shape permutations. Captured at RenderDoc eid 44513 in
`FO4_frame5407.rdc` (captured sha `8c615844e644`, 274 / 8 vs corpus
272 / 8 - +12 bytes, -2 insns, exact sample count).

### Reconstructed

- 272-instruction directional sun-light deferred PS fully transcribed
  to HLSL. Major sections:
  1. Depth sample + matrix select (SHARED CB12[20..27] infrastructure
     - 4th shader in the deferred pipeline to use this pattern).
  2. Octahedral normal decode from t1.
  3. View-space position reconstruction via picked matrix.
  4. Cascade-0 PCF: 8-iteration [loop] with 2 SampleCmp taps per
     iter, stratified Poisson from the embedded ICB, 16 total taps
     averaged with 1/16.
  5. Cascade-1 PCF: same pattern with cb2[14..16] matrix and cb2[22]
     params.
  6. Cascade blend by view-space distance with smoothstep.
  7. Distance fade by cb2[24].x squared-then-squared.
  8. Material-id branch:
     - Material 1 (skin): SSS-style BRDF with rotated cos/sin pairs
       using cb12[28..29], wavelength-dependent absorption.
     - Material non-1: Schlick-Fresnel + GGX-like specular against
       sun direction.
  9. Final composition: shadowed diffuse + specular + ambient AO term.
  10. MRT output: o0 (diffuse / 3) -> RT 389, o1 (specular) -> RT 392.
- Stratified Poisson PCF kernel inlined (32 of the 999 ICB entries -
  loop only consumes 16, so functional but not byte-equivalent).
- `[loop]` attribute on PCF loops preserves the runtime-loop
  structure exactly (4 sample_c_lz asm instructions, matching
  original 8/8 sample count).
- fxc round-trip: 248 vs 272 insns (**-8.8%, within ±10% threshold**),
  samples 8/8 EXACT MATCH, resource bindings + signature EXACT MATCH.

### Open items

- [ ] **Material-non-1 BRDF block is condensed** for readability. The
  original asm at insns 178-241 (~64 instructions) has more granular
  MAD sequences than the reconstruction; adding the granularity would
  push the round-trip closer to 0% but inflate the HLSL significantly.
  Optional polish.
- [ ] **Full 999-entry Poisson ICB inline** for byte-equivalent
  round-trip. Currently 32 entries are inlined; the loop only accesses
  16, so functional output should match. Optional polish for asm-
  exact equivalence.
- [ ] **CB12[28..30] semantic names** for the SSS-style BRDF
  parameters. Currently `cb12_idx28_sss_params` /
  `cb12_idx29_sss_angles` / `cb12_idx30` placeholders. IDA Hex-Rays
  cross-read of `BSDFLightShaderPixelConstants` (AE RVA
  `0x02269B80`) would lock names.
- [ ] **CB2[3..9] + [17..19] + [23] semantic names**. These are
  CB2 slots that exist in the 25-vec4 layout but are not read by this
  shader; the dispatch site C++ populates them. Cross-read closes
  these.
- [ ] **5-peer-cluster disambiguation**: blob 3295 vs 3234/3250/
  3268/3182. The reconstruction is structurally the same across the
  cluster; the exact permutation flags choosing 3295 (vs siblings)
  need `BSDFLightShaderMacros::GetPixelShaderID` cross-read (AE RVA
  `0x0226A030`) or per-BSShader-subclass catalog enrichment.
- [ ] **Cascade-PCF zRef bias** (-0.275 * range_rcp at insns 48, 83).
  The exact bias formula is preserved structurally but the value
  -0.275 may have a semantic name (likely `ShadowDepthBias` or
  `CascadeNormalOffset`).
- [ ] **Per-frame permutation diff**. A second RenderDoc capture
  under different shadow distance / IBL settings could surface which
  of the 5-peer-cluster variants gets dispatched in non-default
  settings.

## Cascade-shadow build chain (resolved)

The 46-dispatch cascade-shadow-build sequence at eids 44848-45252
(building the Texture2DArray that the sun-light PS reads as t5)
resolves to **4 distinct VSM pixel shaders** that share the same
host class:

- BSImagespaceShaderCopyShadowMapToArray
- eids 44848-45252 across 4 PS variants, 46 total dispatches
- writes RT 205+215 -> RT 408 (R16_UNORM cascade atlas) which is the
  Texture2DArray the sun-light deferred PS samples via t5.SampleCmp.

The PSes are simple variance-shadow-map / depth-resolve passes; no
BRDF math. Listed here for completeness rather than as a
reconstruction target.

## Small post-sun MRT PSes (resolved)

Two additional post-sun MRT PSes at eids 44533 (2252b) and 44553
(6380b) write to the same RT 389+392 HDR pair as the sun-light
deferred PS. Inferred roles from shape + write target:

- eid 44533: `post-sun small composite` (likely sky / water).
- eid 44553: terrain or distant geometry deferred contributor.

Both fire before the ambient/IBL pass and write into the same MRT
pair as the sun light, so they are part of the pre-ambient
accumulation. Not reconstructed; documented for completeness.
