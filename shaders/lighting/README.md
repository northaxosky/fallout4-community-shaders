# `shaders/lighting/`

Reconstructed HLSL reference for FO4's deferred lighting pipeline.

This directory is a **reference**, not a runtime asset. Files here are
human-readable reconstructions of Bethesda's precompiled `.fxp` shaders
intended to inform feature implementations elsewhere in the repo.

## Status

| File | Role | Source binding (RT in/out) | REL::ID OG/NG/AE | Status |
|---|---|---|---|---|
| `ambient_ibl_pass.hlsl`     | ambient + image-based lighting consuming kSSAO | reads `kSSAO=28`, `kGbuffer*`; writes `kDiffuseBuffer=58` | (inside `DeferredLightsImpl` `1108521 / 2318312 / 2318312`) | **reconstructed-roundtrip-1.5pct** |
| `deferred_composite.hlsl`   | combine diffuse + specular + albedo | reads `kGbufferAlbedo=22`, `kDiffuseBuffer=58`, `kSpecularBuffer=59`; writes `kMain=3` | `DrawWorld::DeferredComposite` `728427 / 2318313 / 2318313` | **reconstructed-roundtrip-wip** |
| `vls_slice_scatter.hlsl`    | per-slice scatter PS in FO4's VLS (Volumetric Light Scattering) subsystem | reads main depth (t7); writes `kMain=3` (RT 172 in capture) | inside `ImageSpaceEffectVLSLight::Render` (AE RVA `0x022562D0`) / `NVGodrays::RenderVolume` (AE RVA `0x02211740`) | **reconstructed-role-confirmed** |
| `sun_light_deferred.hlsl`   | directional sun-light deferred PS with cascade-shadow PCF | reads gbuffer (albedo/normal/material) + main depth + cascade shadow Texture2DArray; writes RT 389+392 (kDiffuse/kSpec HDR pair) | `DrawWorld::AccumulateSunShadowLightImpl` (REL::IDs `{OG=259940, NG=2318296, AE=2318296}`, AE RVA `0x021eb4f0`) | **reconstructed-roundtrip-8.8pct** |

The `lighting-shader-id-map.json` companion file maps each reconstructed
HLSL to its host REL::ID, OG/NG/AE RVAs, and render-target bindings.

## Reconstruction status

* **`ambient_ibl_pass.hlsl`** - **reconstructed, round-trip +1.5%**.
  Canonical blob: `Shaders011.fxp` blob **3559** (sha1 `7460585eaf76`),
  mnemonic-stream-exact-match sibling of the previously-analysed blob
  3560. 263-instruction ambient/IBL deferred PS including the 9-tap
  SSSS-style bilateral blur. Resource bindings (14 SRVs + 14 samplers +
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
  (reconstructed 2026-05-18; canonical blob 3539). Canonical blob:
  `Shaders011.fxp` blob 3539 (sha1 `861504f6dcbe`), identified by
  mnemonic-stream equivalence against the captured runtime PS at eid
  45368 (sha1 `813c9acec23b`) - same shader, different bytecode
  encoding. The HLSL is a faithful asm-to-source transcription of all
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
* **`sun_light_deferred.hlsl`** - **reconstructed, round-trip -8.8%**.
  Canonical blob: `Shaders011.fxp` blob **3295** (sha1 `50e2618e8d1a`),
  the strongest candidate in a 5-peer cluster of similar permutations.
  272-instruction directional sun-light deferred PS with cascade-shadow
  hardware PCF (16-tap stratified Poisson per cascade, 2 cascades +
  smooth blend), octahedral-encoded normal decode, view-space position
  reconstruction via the shared CB12[20..27] matrix pair (same
  infrastructure as composite + ambient/IBL + VLS slice), material-id-
  branched BRDF (material 1 = skin subsurface-style, non-1 = standard
  Schlick + GGX), MRT output to RT 389 (kDiffuseBuffer equivalent) +
  RT 392 (kSpecularBuffer equivalent). Hosted by
  `DrawWorld::AccumulateSunShadowLightImpl` (cross-runtime confirmed
  REL::IDs `{OG=259940, NG=2318296, AE=2318296}`). Round-trip via
  fxc /T ps_5_0 /O3: 248 vs 272 insns (-8.8%, within the ±10%
  threshold), sample count EXACT match (8/8). The `[loop]` attribute
  keeps the cascade PCF blocks as runtime loops (4 sample_c_lz
  instructions in asm), matching the original exactly. The material-
  non-1 BRDF block is condensed for readability; full asm-granular
  reconstruction would add ~24 more insns.

## Earlier classifier picks (historical context)

The shape-based classifier output that bootstrapped this work landed
on three "hottest representative" candidates per role. Two of them
turned out to be misidentifications, surfaced once the live capture
was cross-referenced against the on-disk corpus:

* **Blob 2122** (initially picked as `deferred-composite`) is actually
  a per-light geometry pass, not a fullscreen composite. The canonical
  composite is blob **3539**.
* **Blob 3147** (initially picked as `directional-sun-light`) belongs
  to a 30+ peer cluster of permutations; no single member is canonical
  for any captured scene. The actual sun-shadow path is the
  cascade-PCF deferred PS at blob **3295**.
* The on-disk corpus survey confirmed that `Shaders011.fxp` plus the
  PE-embedded shader blobs across OG/NG/AE are complete with respect
  to the deferred-stage PSes; earlier "missing" PSes were missing only
  because the D3D loader re-encodes the bytecode (mnemonic-stream
  identical to corpus blobs, byte-different).

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
   * Cross-check by recompiling the reconstruction with `fxc.exe` /
     `dxc.exe` and diffing the resulting DXBC against the original ASM.

Any HLSL committed here is expected to either (a) round-trip to ASM
that closely matches the original bytecode, or (b) be marked WIP with
explicit `// TODO` blocks and a documented instruction-count delta.

## Why these shaders

These four shaders together cover the major paths in FO4's deferred
lighting pipeline: ambient + image-based lighting (with kSSAO
modulation), directional sun light + cascade shadows, volumetric light
scattering accumulation, and final composite. Delivering them
unblocks SSGI integration: the ambient/IBL pass tells us how the
engine applies AO to the ambient term, so the SSGI feature can
integrate at the right boundary instead of post-modulating
`kDiffuseBuffer` (which darkens direct light along with ambient).

Point/spot light passes, fog, and the per-material permutation axes
remain unmapped; they are queued as follow-up work.

## License

These files are derivative reverse-engineering of Bethesda's compiled
shader binaries. They are licensed under this repo's terms (`COPYING`
and `EXCEPTIONS.md`); see each file's header for attribution.
