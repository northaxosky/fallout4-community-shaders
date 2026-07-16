# `shaders/lighting/`

Reconstructed HLSL reference for FO4's deferred lighting pipeline.

This directory is a **reference**, not a runtime asset. Files here are
readable reconstructions of Bethesda's precompiled `.fxp` shaders
intended to inform feature implementations elsewhere in the repo.

## Status

| File | Role | Source binding (RT in/out) | REL::ID OG/NG/AE | Status |
|---|---|---|---|---|
| `ambient_ibl_pass.hlsl`     | ambient + image-based lighting consuming kSSAO | reads `kSSAO=28`, `kGbuffer*`; writes `kDiffuseBuffer=58` | (inside `DeferredLightsImpl` `1108521 / 2318312 / 2318312`) | **reconstructed-roundtrip-1.5pct** |
| `deferred_composite.hlsl`   | combine diffuse + specular + albedo | reads `kGbufferAlbedo=22`, `kDiffuseBuffer=58`, `kSpecularBuffer=59`; writes `kMain=3` | `DrawWorld::DeferredComposite` `728427 / 2318313 / 2318313` | **reconstructed-roundtrip-wip** |
| `deferred_prepass.hlsl`     | geometry pass filling G-buffer (standard opaque permutation) | writes `kGbufferNormal=20`, `kGbufferAlbedo=22`, `kGbufferMaterial=24`, motion vector + aux RTs | `DrawWorld::DeferredPrePass` `56596 / 2318301 / 2318301` | **reconstructed-roundtrip-1.25pct** |
| `vls_slice_scatter.hlsl`    | per-slice scatter PS in FO4's VLS (Volumetric Light Scattering) subsystem | reads main depth (t7); writes `kMain=3` (RT 172 in capture) | inside `ImageSpaceEffectVLSLight::Render` (AE RVA `0x022562D0`) / `NVGodrays::RenderVolume` (AE RVA `0x02211740`) | **reconstructed-role-confirmed** |
| `bsdf_light_deferred.hlsl`  | consolidated BSDFLightShader deferred PS (directional + point/spot permutations via `LIGHT_TYPE` #ifdef) | reads BSDFLight G-buffer aliases `t0=RT26`, `t1=RT27`, `t2=RT30` + main depth + (directional) cascade shadow Texture2DArray / (point) light cookie t7; writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` (R11G11B10F HDR pair; RT 389/392 in RenderDoc captures are runtime resource IDs for those slots, not stable engine enum values) | `DrawWorld::AccumulateSunShadowLightImpl` (REL::IDs `{OG=259940, NG=2318296, AE=2318296}`, AE RVA `0x021eb4f0`) for directional; point dispatched within `DeferredLightsImpl`; spot stub awaits canonical capture | **directional-reconstructed-roundtrip-8.8pct; point-reconstructed-roundtrip-26.5pct; spot STUB** |

The `lighting-shader-id-map.json` companion file maps each reconstructed
HLSL to its host REL::ID, OG/NG/AE RVAs, and render-target bindings.

## Reconstruction status

* **`ambient_ibl_pass.hlsl`** - **reconstructed, round-trip +1.5%**.
  Canonical blob: `Shaders011.fxp` blob **3559** (sha1 `7460585eaf76`),
  mnemonic-stream-exact-match sibling of the structurally-equivalent
  blob 3560. 263-instruction ambient/IBL deferred PS including the
  9-tap SSSS-style bilateral blur. Resource bindings (14 SRVs + 14 samplers +
  3 CBs) + signature exact-match. Round-trip via fxc /T ps_5_0 /O3: 269
  vs 265 insns (+1.5%, within the ±10% threshold for this larger
  shader). Sample count 41 vs 44 (3 short due to a missing +1.28 ring
  tap). Key findings:
  * **AO-application boundary** at the final multiply on the
    combined ambient+IBL term (no direct-light contamination).
  * **kSSAO write timeline**: SAO writes at
    `Render_PreUI +0x036b`; ambient PS reads at `+0x039c`. Hook point:
    `RegisterPreDeferredLightsImpl` (already implemented).
  * **SRV map**: 14 SRVs identified via RenderDoc capture
    `FO4_frame5407.rdc` event-id 45345. High-confidence: t1=kGbufferNormal,
    t4=kGbufferAlbedo, t7=main depth, t8=IBL probe cubemap array,
    t9=kSSAO, t14=kMainPreAlpha (lit scene), t15=kMainDepthMips.
    Medium-confidence: t2/t3=gbuffer aux, t5/t11=ambient diffuse pair,
    t6/t10/t12=screen-space ambient HDR scratch.
  * **Output**: o0 = kDiffuseBuffer (R11G11B10F).
* **`deferred_composite.hlsl`** - **reconstructed, round-trip WIP**
  (canonical blob 3539). Canonical blob: `Shaders011.fxp` blob 3539
  (sha1 `861504f6dcbe`), identified by mnemonic-stream equivalence
  against the captured runtime PS at eid 45368 (sha1 `813c9acec23b`) -
  same shader, different bytecode encoding. The HLSL is a faithful asm-to-source transcription of all
  90 instructions: resource bindings exact-match, sample count
  exact-match (6/6), signature exact-match. Round-trip via fxc
  `/T ps_5_0 /O3` produces 108 insns vs 90 original (+20%): structural
  fidelity verified, instruction-count delta documented. CB12 field
  semantics and texture RT-index mapping are placeholder names
  (`cb12_idx<N>_*` and inferred role names); finalizing requires IDA
  Hex-Rays cross-read of `DrawWorld::DeferredComposite`.
* **`vls_slice_scatter.hlsl`** - **reconstructed, role confirmed**
  (renamed from `directional_sun_light.hlsl` after the captured PS at
  eid 45401 (sha `46b911cb8053`) was confirmed to be a per-slice
  scatter PS in FO4's Volumetric Light Scattering (VLS) subsystem, not
  a directional sun-shadow PS). PDB symbol walk surfaced
  `ImageSpaceEffectVLS::Render` (AE RVA `0x02256F40`),
  `ImageSpaceEffectVLSLight::Render` (AE RVA `0x022562D0`),
  `BSImagespaceShaderVLSSliceScatterInterp::Render` (AE RVA
  `0x021A18B0`), and `NVGodrays::RenderVolume(BSShadowLight*, int)`
  (AE RVA `0x02211740`) as the implementing chain. The 22-dispatch
  pattern at eids 45401-45623 is N slices × M shadow-lights for VLS
  accumulation into `kMain`. Round-trip from the prior reconstruction
  is unchanged (+33.9% insns vs original; structural fidelity verified).
* **`deferred_prepass.hlsl`** - **reconstructed, round-trip -1.25%**.
  Standard-opaque G-buffer prepass PS for FO4's deferred pipeline.
  Canonical runtime sha1: `c493970c042c...` (eid 13220 in
  `FO4_frame9483.rdc`). 80-instruction PS that fills 6 MRT outputs:
  albedo (`kGbufferAlbedo`), 2-channel octahedral normal
  (`kGbufferNormal`), packed material data (`kGbufferMaterial`), two
  auxiliary G-buffer slots (scroll UVs + specular tint), and the
  screen-space motion vector via current-frame vs previous-frame
  world-to-clip dp4 + perspective-divide. Resource bindings (3 SRVs
  `t0/t1/t2` + 3 default samplers + CB12[41] / CB2[6]) and signature
  exact-match. Round-trip via fxc /T ps_5_0 /O3: 79 vs 80 insns
  (-1.25%), 3/3 samples. Hosted by `DrawWorld::DeferredPrePass`
  (REL::IDs `{OG=56596, NG=2318301, AE=2318301}`). 7 other prepass
  permutations (skin / hair / decal / projected / two-sided /
  alpha-test) captured at eids 13241 / 13259 / 13507 / 13546 / 31244 /
  38343 / 39205 are documented but NOT reconstructed; they share
  resource shape with the standard-opaque variant but differ in body
  math.
* **`bsdf_light_deferred.hlsl`** - **directional + point branches reconstructed; spot is a stub**.
  Consolidated BSDFLightShader deferred PS, parameterized by
  `LIGHT_TYPE` (DIRECTIONAL=1 default; POINT=2 reconstructed; SPOT=3 is a stub).
  This file replaces the prior `sun_light_deferred.hlsl`; the directional
  branch's bytecode is byte-identical to the deleted file (verified via
  sha1 comparison of fxc /O3 output: `085625AF00823BEA6806625AEF9328C055C9E4F2`).
  Canonical blob (directional): `Shaders011.fxp` blob **3295** (sha1
  `50e2618e8d1a`). 272-instruction directional sun-light deferred PS
  with cascade-shadow hardware PCF (16-tap stratified Poisson per
  cascade, 2 cascades + smooth blend). Round-trip via fxc /T ps_5_0 /O3:
  248 vs 272 insns (-8.8%), sample count EXACT match (8/8).

  **Point-light branch** (`-D LIGHT_TYPE=2`) reconstructed from
  FO4_frame9483.rdc eid 46771 (sha1 `3f1f708c0175`, 204 insns,
  `bsdf-light-unshadowed` permutation): unshadowed point light with
  radial attenuation curve (`cb2[1].w` radius + `cb2[3].xyz` curve
  params) and an octahedral 2D light-cookie sample at `t7` driven by a
  `cb2[11..14]` light-space transform. Resource bindings exact-match
  (5 SRVs `t0/t1/t2/t3/t7`, 5 default samplers `s0/s1/s2/s3/s7` - no
  comparison sampler), 5/5 samples, signature exact-match (o0 + o1).
  Round-trip 150 vs 204 insns (-26.5%, BRDF condensation comparable to
  the sun_light precedent's -28.7%).

  Spot stub at the end of the file documents the expected math sketch;
  reconstruction awaits a canonical spot-light capture.

## Workflow

The reconstruction pipeline is:

1. **Mechanical stage** (automated): extract `Data/Fallout4 - Shaders.ba2`,
   disassemble every blob in `Shaders011.fxp`, and group PSes by
   resource-binding shape into candidate clusters per deferred-pipeline
   role.
2. **Manual stage** (per-shader):
   * Pick a representative from the dominant shape bucket; cross-check
     against the live capture sha for the role to lock the canonical
     blob.
   * For each target asm, identify CB layout, texture slots, and BRDF
     shape. Use `// TODO: identify` rather than guessing.
   * Cross-check by recompiling the reconstruction and diffing its ASM
     against the original. `scripts/shaders/fetch-shader-corpus.ps1` extracts the
     corpus blobs from the local game install and `scripts/shaders/shader_corpus_diff.py`
     reports the drop-in contract match plus the instruction-stream delta.

Any HLSL committed here is expected to either (a) round-trip to ASM
that closely matches the original bytecode, or (b) be marked WIP with
explicit `// TODO` blocks and a documented instruction-count delta.

## Why these shaders

These shaders together cover the major paths in FO4's deferred lighting
pipeline: the G-buffer prepass (geometry pass that fills albedo / normal
/ material / motion vector), ambient + image-based lighting (with kSSAO
modulation), directional sun light + cascade shadows, unshadowed
point lights, volumetric light scattering accumulation, and final
composite. Delivering them unblocks SSGI integration: the ambient/IBL
pass tells us how the engine applies AO to the ambient term, so the
SSGI feature can integrate at the right boundary instead of post-
modulating `kDiffuseBuffer` (which darkens direct light along with
ambient).

Spot lights, fog, shadowed-point-light permutations, and per-material
prepass permutation axes remain unmapped; they are queued as follow-up
work.

## License

These files are derivative reverse-engineering of Bethesda's compiled
shader binaries. They are licensed under this repo's terms (`LICENSE`
and `EXCEPTIONS.md`); see each file's header for attribution.
