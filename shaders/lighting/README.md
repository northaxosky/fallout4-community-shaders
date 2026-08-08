# `shaders/lighting/`

Reconstructed HLSL for FO4's deferred lighting pipeline.

This directory is **live product content**. `src/Render/ShaderInjection.cpp`
compiles these readable reconstructions of Bethesda's precompiled `.fxp`
shaders and injects registered permutations at runtime.

## Status

| File | Role | Source binding (RT in/out) | REL::ID OG/NG/AE | Status |
|---|---|---|---|---|
| `ambient_ibl_pass.hlsl`     | interior/reference ambient + IBL, blob 3559 | reads `kSSAO=28`, `kGbuffer*`; writes `kDiffuseBuffer=58` | (inside `DeferredLightsImpl` `1108521 / 2318312 / 2318312`) | **exec-diff-zero** |
| `ambient_ibl_pass_runtime.hlsl` | exterior/runtime BSDFComposite family, PSIDs `0x10B60` and `0xB60` | Tilelight adds t11 ambient diffuse B and t12 blurred SSLR | same | **both live-mapped; exec-diff-zero** |
| `deferred_composite.hlsl`   | combine diffuse + specular + albedo | reads `kGbufferAlbedo=22`, `kDiffuseBuffer=58`, `kSpecularBuffer=59`; writes `kMain=3` | `DrawWorld::DeferredComposite` `728427 / 2318313 / 2318313` | **reconstructed-roundtrip-wip** |
| `deferred_prepass.hlsl`     | geometry pass filling G-buffer (standard opaque permutation) | writes `kGbufferNormal=20`, `kGbufferAlbedo=22`, `kGbufferMaterial=24`, motion vector + aux RTs | `DrawWorld::DeferredPrePass` `56596 / 2318301 / 2318301` | **reconstructed-roundtrip-1.25pct** |
| `vls_slice_scatter.hlsl`    | per-slice scatter PS in FO4's VLS (Volumetric Light Scattering) subsystem | reads main depth (t7); writes `kMain=3` (RT 172 in capture) | inside `ImageSpaceEffectVLSLight::Render` (AE RVA `0x022562D0`) / `NVGodrays::RenderVolume` (AE RVA `0x02211740`) | **reconstructed-role-confirmed** |
| `bsdf_light_deferred.hlsl`  | consolidated BSDFLightShader deferred PS (directional + point/spot permutations via `LIGHT_TYPE` #ifdef) | reads BSDFLight G-buffer aliases `t0=RT26`, `t1=RT27`, `t2=RT30` + main depth + (directional) cascade shadow Texture2DArray / (point) light cookie t7; writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` (R11G11B10F HDR pair; RT 389/392 in RenderDoc captures are runtime resource IDs for those slots, not stable engine enum values) | `DrawWorld::AccumulateSunShadowLightImpl` (REL::IDs `{OG=259940, NG=2318296, AE=2318296}`, AE RVA `0x021eb4f0`) for directional; point dispatched within `DeferredLightsImpl`; spot stub awaits canonical capture | **directional-reconstructed-roundtrip-8.8pct; point-live-exec-diff-zero; spot STUB** |
| `bsdf_light_deferred_shadow_only.hlsl` | BSDFLightShader deferred PS, native `DIRECTIONAL`+`SHADOW_ONLY` family; carries the `FILTER_*` axis (none / PCF1 / PCF9 / PCSS / POISSON / PCSSPOISSON) | reads `t1=RT27`, `t2=RT30`, main depth `t3`, cascade shadow Texture2DArray at `t4` (raw) and/or `t5` (comparison); writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | same host as the directional path above | **native-shex-identical, 6/6** |
| `bsdf_light_deferred_shadow_only_blendsplit.hlsl` | BSDFLightShader deferred PS, native `DIRECTIONAL`+`SHADOW_ONLY`+`BLENDSPLIT` family at `DIRSPLITS=1`; a three-wide `FILTER_*` axis (PCF1 / PCF9 / POISSON) crossed with `AMBIENT` | reads main depth `t3` and the cascade shadow Texture2DArray at `t5` (comparison only), plus `t1=RT27` and `t2=RT30` under `AMBIENT`; writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | same host as the directional path above | **native-shex-identical 6/6; native-abi-equal 6/6 (read-counts exact, no axis exempt)** |
| `bsdf_light_deferred_dirsplits1.hlsl` | BSDFLightShader deferred PS, native full-BRDF `DIRECTIONAL`+`SHADOW` family at `DIRSPLITS=1`; comparison filters PCF1 / PCF9 / POISSON crossed with `AMBIENT` | reads `t0..t3` (G-buffer aliases + main depth) and cascade shadow Texture2DArray `t5` with comparison sampler `s5`; writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | same host as the directional path above | **producer WARP PASS 12/12 fixtures; native-abi-equal 6/6; native SHEX differs 6/6** |
| `bsdf_light_deferred_dirsplits2.hlsl` | BSDFLightShader deferred PS, native full-BRDF `DIRECTIONAL`+`SHADOW` family at `DIRSPLITS=2`; carries the `FILTER_*` axis (none / PCF1 / PCF9 / PCSS / POISSON) crossed with `AMBIENT` × `BLENDSPLIT` × `IGNOREROUGHNESS` | reads `t0..t3` (G-buffer aliases + main depth), cascade shadow Texture2DArray at `t4` (raw) and/or `t5` (comparison); writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | same host as the directional path above | **native-abi-equal, 29/29 (read-counts exact, no axis exempt)** |
| `bsdf_light_deferred_dirsplits3.hlsl` | BSDFLightShader deferred PS, native full-BRDF `DIRECTIONAL`+`SHADOW` family at `DIRSPLITS=3`; carries the `FILTER_*` axis (none / PCF1 / PCF9 / PCSS / PCSSPOISSON / POISSON) crossed with `AMBIENT` × `BLENDSPLIT` × `IGNOREROUGHNESS` | reads `t0..t3` (G-buffer aliases + main depth), cascade shadow Texture2DArray at `t4` (raw) and/or `t5` (comparison); writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | same host as the directional path above | **native-abi-equal, 27/27 (read-counts exact, no axis exempt)** |
| `bsdf_light_deferred_unshadowed.hlsl` | BSDFLightShader deferred PS, the native **unshadowed** light families - `DIRECTIONAL` (5 blobs) and full-BRDF `POINTOMNI` (6 blobs), no `SHADOW` macro, so no shadow resource at all | reads `t0..t3` only (G-buffer aliases + main depth) with `s0..s3` mode_default; writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | same hosts as the directional and point paths above | **native-abi-equal, 11/11 (read-counts exact; new point rows execution-unproven)** |
| `bsdf_light_deferred_gobo.hlsl` | BSDFLightShader deferred PS, unshadowed `POINTOMNI`+`GOBOPROJECTION` at `DIRSPLITS=2`; Wave 1 covers `SPECULAR` × `IGNORERIM` | reads `t0..t3` plus light cookie `t7`, all Texture2D with `s0..s3,s7` mode_default; writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | point dispatch within `DeferredLightsImpl` | **native-abi admission, Wave 1 C1: 4 rows; 2 IGNOREROUGHNESS controls compile-only** |
| `bsdf_light_deferred_attenuation_only.hlsl` | BSDFLightShader deferred PS, native `POINTOMNI`+`ATTENUATION_ONLY` family at `DIRSPLITS=2` (one blob, four routes) | reads main depth `t3` only with `s3` mode_default; writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | point-light dispatch within `DeferredLightsImpl` | **native-abi-equal, 1/1 (read-counts exact)** |

The `lighting-shader-id-map.json` companion file maps each reconstructed
HLSL to its host REL::ID, OG/NG/AE RVAs, and render-target bindings.

## Reconstruction status

* **`ambient_ibl_pass.hlsl`** preserves the interior/reference permutation:
  `Shaders011.fxp` blob **3559**, sha1 `7460585eaf76`, CB12[31].
  Its 33 declarations and 44 samples match the native contract, and its
  shaped execution diff remains zero.
* **`ambient_ibl_pass_runtime.hlsl`** is the live exterior BSDFComposite family
  source for Tilelight PSID `0x10B60` (blob **3560**, sha1 `2b6e36c08aca`) and
  no-Tilelight PSID `0xB60` (blob **3495**, sha1 `6d726d0fe6b6`). Both use
  CB12[47] and the same post-AO fog/color stack. `TILELIGHT` adds t11 ambient
  diffuse B and t12 blurred SSLR; the hardened oracle validates each build
  against its own blob and rejects the no-Tilelight build against blob 3560.
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

  **Point-light branch** (`-D LIGHT_TYPE=2`) uses the live canonical
  QASmoke + Pip-Boy-light shader from `FO4_frame24669`: sha1
  `9969e800683c8a7c8afc25f41582415d79cbe47e`, 6,432 bytes, 205
  executable instructions, also present byte-for-byte at `Shaders011.fxp`
  blobs **3287** and **3316**. Its exact contract is CB12[30], CB2[15];
  `t0/t1/t2/t3/t7` Texture2D with default samplers
  `s0/s1/s2/s3/s7`; SV_POSITION plus unused POSITION14; and
  two MRT outputs. The body starts with `v0.xy * cb2[0].xy`, without a
  secondary screen scale. The point path applies radial attenuation,
  the complete skin/default BRDF, and a dual-paraboloid t7 cookie driven
  by `cb2[11..14]`.
  The producer conformance evidence is CONTRACT PASS (15/15 declarations,
  5/5 samples; 205 corpus vs 198 reconstructed executable instructions).
  Historical frame9483 sha1 `3f1f708c01758a3d20267e5c1b7a6472b8c9d336`
  is a different permutation: CB2[23], a `cb2[22]` secondary screen
  scale, no POSITION14 input, and an octahedral cookie projection. It is
  not the live canonical point shader.

  Spot stub at the end of the file documents the expected math sketch;
  reconstruction awaits a canonical spot-light capture.

  **POINTOMNI+SHADOW reconstruction** (`-D LIGHT_TYPE=3 -D POINTOMNI=1 -D SHADOW=1`).
  A producer audit found 31 native POINTOMNI+SHADOW records whose declared ABI
  and FXP constant tables are identical to the already-proven POINTSPOT
  profiles. An internal selector,
  `FO4_PROJECTED_SHADOW_FAMILY`, admits both families into the same block, so
  POINTOMNI is admitted without being rewritten into POINTSPOT and without
  either family's native macro set being touched.
  All 31 compile and are contract-equal to their corpus blob — CB sizes and
  indexing mode, SRV slots and types, sampler slots and modes, and the IO
  signature all match: `CB12[30]` + `CB2[21]` immediateIndexed, `t0..t3` with
  `s0..s3` mode_default, the shadow map at `t5`/`s5` mode_comparison under
  `FILTER_PCF1` (9) / `FILTER_PCF9` (11) / `FILTER_POISSON` (8) or at `t4`/`s4`
  mode_default in the 3 unfiltered records - 9 + 11 + 8 + 3 = 31, this family's
  own count, not to be read against the `DIRSPLITS=2` figures below - plus
  `t7`/`s7` in the 10 `GOBOPROJECTION` records.

  Identical ABI is not identical lookup. POINTSPOT projects planar and always
  samples slice 0; native POINTOMNI is dual paraboloid, and all three of its
  lookups are reconstructed from the native disassembly.

  *Base projection* — hemisphere from the *pre-divide*
  biased `dot(c13,p) * 0.5 + 0.5 < 0`, which puts the boundary at raw `-1` and
  not `0`; array slice `back ? 1 : 0`; atlas Y
  `1 - (back ? uv.y : 1 - uv.y) * scale`; reference `length(q) / radius`; and no
  zero guard on the paraboloid divide, matching native.

  *`HALFOMNI`* (12 of the 31) keeps the `+Z` pole and slice 0 and returns
  unconditional zero on the rejected half — same `dp4 c13` → `mad *0.5+0.5`
  sequence, but `ge ... 0` to reject rather than select.

  *`GOBOPROJECTION`* (10 of the 31) derives its cookie from the same omni
  intermediate. Full omni packs the front hemisphere low and the mirrored back
  hemisphere high, `float2(uv.x, back ? 1 - 0.5*uv.y : 0.5*uv.y)`; HALFOMNI
  samples the fixed `+Z` UV directly with no packing. Two distinct mappings, not
  one.

  **All 31 pass the producer's WARP execution-diff oracle at zero divergent
  pixels.** POINTOMNI *without* `SHADOW` is a different ABI and stays on
  `LIGHT_TYPE=2`; the `LIGHT_TYPE=3` guards reject it explicitly.

  `scripts/shaders/verify-pointomni-admission.ps1` (CTest
  `PointOmniShadowAdmission`) re-measures all 31 and fails closed. It also
  asserts that 16 malformed macro sets still refuse to compile — POINTOMNI
  without `SHADOW`, POINTOMNI with `SPOT`/`POINTSPOT`, both
  `ATTENUATION_ONLY` misroutes, two `FILTER_*` at once,
  `FILTER_PCSS`/`FILTER_PCSSPOISSON` anywhere on this path
  (all 15 PCSS and all 3 PCSSPOISSON blobs in the archive are DIRECTIONAL),
  `HALFOMNI` without POINTOMNI, and POINTOMNI+SHADOW misrouted to
  `LIGHT_TYPE=2` — and that the 9 native POINTOMNI-without-SHADOW macro sets
  still compile on `LIGHT_TYPE=2`, so the guards cannot over-reach.
* **`bsdf_light_deferred_attenuation_only.hlsl`** - **native ABI equal, 1/1**.
  This source exclusively owns
  `POINTOMNI + ATTENUATION_ONLY + RGBSPEC + DIRSPLITS=2`, represented by
  native blob `aa5cd5f492d921546a2b9cf66d34eae9baedf63f` at four archive routes.
  Its contract is `CB12[28]`, `CB2[4]`, main depth `t3` with `s3`
  mode_default, unused `POSITION14`, and the two-MRT output signature. It runs
  only depth reconstruction and radial attenuation: no G-buffer BRDF inputs,
  cookie, shadow map or filter axis are declared.

  `scripts/shaders/attenuation-only-native-abi.json` and CTest
  `PointOmniAttenuationOnlyAdmission` pin the declaration set plus every
  constant-register read count and reject every other known Light macro. The
  legacy adapter and unshadowed full-BRDF source reject this family explicitly,
  so it cannot compile through a broader source by accident.

  This is a consumer-owned ABI and static read-count claim only. Full-DXBC
  equality and numerical execution proof remain producer-owned and pending.

* **`bsdf_light_deferred_shadow_only.hlsl`** - **native SHEX identical, 6/6**.
  The native `DIRECTIONAL` + `SHADOW_ONLY` family, and the only place in the
  archive where the `FILTER_*` axis appears as a controlled minimal pair: six
  blobs sharing `DIRECTIONAL + DIRSPLITS=1 + RGBSPEC + SHADOW + SHADOW_ONLY +
  SPECULAR` and differing only in the filter macro. It is a sibling of
  `bsdf_light_deferred.hlsl` rather than another `#ifdef` inside it, because
  that file's bytes are pinned by `source_sha256` in the producer conformance
  manifest, which only the producer may republish.

  All six permutations compile to a DXBC whose SHEX chunk - the instruction
  stream plus the immediate constant buffer - is byte-identical to the archive
  blob's, so the reconstruction is not merely equivalent but the same program:

  | Filter | Archive blob sha1 | Blob bytes | SHEX bytes |
  |---|---|---|---|
  | (none)               | `964e9b82cde7aece` | 2328  | 2112  |
  | `FILTER_PCF1`        | `0bff5e0ecdf73249` | 2304  | 2088  |
  | `FILTER_PCF9`        | `ba94fedf2ed412c7` | 2720  | 2504  |
  | `FILTER_PCSS`        | `53e2fcf10e89547e` | 4068  | 3852  |
  | `FILTER_POISSON`     | `99a112e7bc7fc4fb` | 19016 | 18800 |
  | `FILTER_PCSSPOISSON` | `e5ef2e946298f7ee` | 20016 | 19800 |

  `scripts/shaders/verify-filter-axis.ps1` (CTest `FilterAxisNativeShex`)
  re-measures this and fails closed. Its pinned hashes are taken from the game
  bytecode, so the gate cannot bless the repository's own output. The exact
  1000-entry Poisson kernel the last two permutations declare lives in
  `shadow_poisson_kernel.hlsli`, transcribed from the SHEX CUSTOM_DATA token at
  full float32 precision.

  Scope: AE 1.11.221 archive blob set, `DIRSPLITS=1` only. The two other
  complete six-variant filter families in the archive
  (`BLENDSPLIT + DIRSPLITS=3` with and without `AMBIENT`) carry the full BRDF
  and are not reconstructed here; they belong to
  `bsdf_light_deferred_dirsplits3.hlsl`. The `DIRSPLITS=1` blobs that do carry
  `BLENDSPLIT` are a separate three-wide filter family and live in
  `bsdf_light_deferred_shadow_only_blendsplit.hlsl`.
* **`bsdf_light_deferred_shadow_only_blendsplit.hlsl`** - **native SHEX
  identical, 6/6; native ABI equal, 6/6**. The native
  `DIRECTIONAL` + `SHADOW_ONLY` + `BLENDSPLIT` family at `DIRSPLITS=1`: six
  blobs, 18 archive route occurrences, sharing
  `DIRECTIONAL + DIRSPLITS=1 + RGBSPEC + SHADOW + SHADOW_ONLY + SPECULAR +
  BLENDSPLIT` over a three-wide filter axis crossed with `AMBIENT`. A sibling of
  `bsdf_light_deferred_shadow_only.hlsl` rather than a macro inside it, because
  that file's bytes are pinned by `FilterAxisNativeShex` and because
  `BLENDSPLIT` changes the body rather than the epilogue.

  `BLENDSPLIT` removes the material-1 slope bias entirely: `FILTER_PCF1` and
  `FILTER_PCF9` compare the projected z unbiased, and `FILTER_POISSON` uses the
  fixed 0.275 scaled by the reciprocal cascade world depth range with no
  per-material branch. Without `AMBIENT` the split shadow goes to both MRTs and
  the shader declares only `t3/s3` and the comparison tap `t5/s5`. With
  `AMBIENT` it adds `t1/s1` and `t2/s2`, evaluates the `cb2[6..8]`
  DirectionalAmbient rows twice - once against the decoded normal, once against
  the reflection vector - and suppresses the reflected term outright on material
  code 1. There is no unfiltered, `FILTER_PCSS` or `FILTER_PCSSPOISSON` blob in
  this family and no raw `t4/s4` tap in any of its declaration sets, so all
  three are rejected rather than reconstructed.

  | Filter | `AMBIENT` | Archive blob sha1 | Blob bytes | SHEX bytes |
  |---|---|---|---|---|
  | `FILTER_PCF1`    | no  | `4e89dd81f40121ca` | 1596  | 1380  |
  | `FILTER_PCF9`    | no  | `1eb732dd3f746afb` | 2012  | 1796  |
  | `FILTER_POISSON` | no  | `e928ab5f44eede45` | 18348 | 18132 |
  | `FILTER_PCF1`    | yes | `2b04bed67a564153` | 3076  | 2860  |
  | `FILTER_PCF9`    | yes | `e3b027b8c5549dc4` | 3500  | 3284  |
  | `FILTER_POISSON` | yes | `c56b2b7862b68c7a` | 19828 | 19612 |

  All six rows compile to a DXBC container that is byte-identical to the archive
  blob, pinned by `scripts/shaders/ds1-blendsplit-native-shex.json` (CTest
  `DirSplits1BlendSplitNativeShex`). The `AMBIENT` epilogue only lands on the
  native instruction order when both MRTs are written as one four-wide add of a
  split-shadow float4 and an ambient float4: written as an `.xyz` store followed
  by `output.specular.w = 1.0`, fxc sinks the dependency-free `mov o1.w, l(1.0)`
  three slots past the `o0` block and the container stops matching, at every
  optimisation level. All six are additionally gated on declarations, constant
  read-sets, read-counts and immediate-constant rows by
  `scripts/shaders/ds1-blendsplit-native-abi.json` (CTest
  `DirSplits1BlendSplitAdmission`), whose 18 rejected macro sets keep the
  admission from widening.
* **`bsdf_light_deferred_dirsplits1.hlsl`** - **producer WARP PASS; native ABI
  equal, 6/6; native SHEX differs, 6/6**. The full-BRDF `DIRECTIONAL + SHADOW +
  SPECULAR + RGBSPEC + DIRSPLITS=1` sibling covers exactly the comparison-filter
  grid PCF1 / PCF9 / POISSON crossed with `AMBIENT`. The exact 517-line source is
  bound to producer commit `5b08e2a8` and SHA-256 `6ac7815128340a80`; producer
  receipt `47223351308f36b` records both WARP fixtures passing for every row and
  all nine mutants caught in both fixtures.

  | Filter | `AMBIENT` | Archive blob sha1 | Native SHEX | Candidate SHEX |
  |---|---|---|---:|---:|
  | `FILTER_PCF1`    | no  | `9fc11553c6068eac` | 6044 B | 5964 B |
  | `FILTER_PCF1`    | yes | `cf3f9141478b2449` | 6768 B | 6768 B |
  | `FILTER_PCF9`    | no  | `aa721295cd3b1ff8` | 6460 B | 6388 B |
  | `FILTER_PCF9`    | yes | `b732fcfa4b24e58f` | 7192 B | 7192 B |
  | `FILTER_POISSON` | no  | `02427236dcf3dc12` | 22796 B | 22716 B |
  | `FILTER_POISSON` | yes | `a1d88864cd30485d` | 23520 B | 23512 B |

  The equal-size PCF ambient pairs still have different SHEX hashes; all six
  native/candidate identities are recorded as false diagnostic evidence in
  `scripts/shaders/dirsplits1-native-abi.json`. There is deliberately no SHEX
  equality gate. CTest `DirSplits1DirectionalAdmission` instead fails closed on
  the source and compiler identities, native declarations, constant read-sets,
  exact read-counts, and the exact immediate-constant sequence (1000 rows for
  POISSON, zero otherwise). Its 18 rejected cases hold the top-level source
  boundary. The unproven DIRSPLITS=1 PCSS pair remains rejected.
* **`bsdf_light_deferred_dirsplits2.hlsl`** - **native ABI equal, 29/29**.
  The native full-BRDF `DIRECTIONAL` + `SHADOW` family at `DIRSPLITS=2`: the
  layer above `SHADOW_ONLY`, where the solved filter bodies meet the complete
  lighting core. Twenty-nine archive blobs share
  `DIRECTIONAL + DIRSPLITS=2 + RGBSPEC + SHADOW + SPECULAR` and vary over four
  independent axes - `FILTER_*` (none / PCF1 / PCF9 / PCSS / POISSON) crossed
  with `AMBIENT`, `BLENDSPLIT` and `IGNOREROUGHNESS`. It is a sibling file for
  the same reason as `bsdf_light_deferred_shadow_only.hlsl`: the consolidated
  file's bytes are pinned by `source_sha256` in the producer conformance
  manifest.

  The 29 blobs collapse to exactly three declaration groups, and the group is a
  function of the filter alone - `AMBIENT`, `BLENDSPLIT` and `IGNOREROUGHNESS`
  move the instruction stream but not the contract. Every group declares
  `CB12[31]` + `CB2[25]` immediateIndexed, `t0..t3` with `s0..s3` mode_default,
  and the two-MRT signature. Rows are in the same filter order the axis is
  listed in above, so the blob column reads 3 / 6 / 7 / 6 / 7:

  | Filter | Blobs | Shadow map declarations |
  |---|---|---|
  | (none)           | 3 | `t4`/`s4` mode_default (raw tap, compare in the shader) |
  | `FILTER_PCF1`    | 6 | `t5`/`s5` mode_comparison |
  | `FILTER_PCF9`    | 7 | `t5`/`s5` mode_comparison |
  | `FILTER_PCSS`    | 6 | `t4`/`s4` mode_default **and** `t5`/`s5` mode_comparison |
  | `FILTER_POISSON` | 7 | `t5`/`s5` mode_comparison |

  That is 3 in `raw_t4_s4`, 20 in `cmp_t5_s5` and 6 in `raw_t4_cmp_t5`.

  `BLENDSPLIT` is one coupled axis, not two independent edits: it removes the
  `cb2[9].w` split-distance gate *and* adds the smoothstep band blend between
  the cascades, and the two always occur together. Without it the band term is
  a literal `1.0` and `cb2[9]` carries `SplitDistances`; with it
  `DirectionalAmbient` extends over `cb2[6..9]` and `cb2[9]` is never read as a
  distance. `DIRSPLITS=2` also differs from the `SHADOW_ONLY` family in what it
  omits: no slope-scaled depth bias and no `0.999999` reference clamp. Only
  `FILTER_POISSON` biases at all, per cascade (0.275 then 1.0).

  `FILTER_PCSSPOISSON` fails closed here: the archive has no `DIRSPLITS=2` blob
  carrying it, only one at `DIRSPLITS=1` and two at `DIRSPLITS=3`. Fabricating
  it would be a fidelity claim with no evidence behind it. Eleven of the 29
  carry `IGNOREROUGHNESS`. Material sampling remains live for the specular
  exponent and material-code-1 hair path, while the default branch uses Lambert
  visibility and omits the roughness-scaled rim term. `AMBIENT` variants replace
  the roughness-dependent exponent with the native fixed square.

  `scripts/shaders/verify-native-abi-admission.ps1` (CTest
  `DirSplits2DirectionalAdmission`) re-measures all 29 against
  `dirsplits2-native-abi.json` and fails closed. Each entry pins three things
  taken from the game bytecode - the blob's declaration set, the set of
  constant-buffer registers its body reads, and how many times it reads each -
  so the gate cannot bless this repository's output. The declaration set is
  checked in both places `fxc` records a constant-buffer size, SHEX and RDEF,
  because those are derived independently and only their agreement pins the
  buffer a host actually sees.

  Those three pins are progressively finer. The read-set separates macro sets
  the declarations cannot: a `BLENDSPLIT` blob reads `cb2[6..8]` and never
  `cb2[9]`, while its plain counterpart reads `cb2[9]` as `SplitDistances`. The
  read-count is finer still and exact on all 29 entries. Each of the 11
  `IGNOREROUGHNESS` blobs drops `cb2[1]` from 5 reads to 3 and `cb2[2]` from 6
  reads to 5 against its twin, matching the Lambert visibility and deleted rim
  term. The manifest declares no `scope.count_exemption_axis`, so every entry is
  held to its native per-register counts.


  The gate also asserts that 16 malformed or out-of-scope macro sets still
  refuse to compile - `FILTER_PCSSPOISSON`, `DIRSPLITS` of 1/3/4/absent, two
  `FILTER_*` at once, `SHADOW_ONLY`, a missing
  `SHADOW`/`SPECULAR`/`RGBSPEC`/`DIRECTIONAL`, and `DIRECTIONAL` crossed with
  `POINTOMNI`/`POINTSPOT`/`SPOT`/`HALFOMNI` - and that the 11 axis combinations
  that are legal natively but happen to have no archive blob still compile, so
  the guards cannot over-reach.

  This is an ABI claim, not SHEX equality. The declarations, constant read-sets
  and constant read-counts on all 29 bodies are measured; the BRDF core is a
  structural reconstruction, and execution proof stays with the
  producer oracle. The verifier is family-agnostic and driven entirely from its
  manifest, which is how the `DIRSPLITS=3` layer landed: an evidence file and a
  test registration, not a third copy of the script.

* **`bsdf_light_deferred_unshadowed.hlsl`** - **native ABI equal, 11/11, read-counts
  exact on every entry; 987c4e79 fixed-square reconstructed**.
  The native **unshadowed** light layer. Eleven archive blobs have no `SHADOW` macro,
  so their contract has no shadow texture and no shadow sampler: five `DIRECTIONAL`
  and six full-BRDF `POINTOMNI`. What this source owns is unshadowed lighting - not a shadow
  selection, and not a cascade count. State that carefully, because the framing is
  itself an evidence claim: absence of `SHADOW` is **not** a third value on the
  shadow-resource axis. `SHADOW` is simply inactive here, so the axis does not exist
  for these eleven and nothing may be grouped or compared along it. The `FILTER_*` axis
  selects among raw `t4`/`s4`, comparison `t5`/`s5`, or both only once `SHADOW` is
  proven active; with `SHADOW` absent there is no shadow resource to select, which is
  why every `FILTER_*` is rejected outright here rather than mapped to a "no filter"
  case.

  All eleven declare `t0..t3` as `texture2d` with `s0..s3` mode_default, the same two
  input semantics and the same two MRT outputs, and all read `CB12[20..29]`. They are
  **not** one contract, though - the constant-buffer sizes split them into four
  groups, which is why removing the shadow declarations from a shadowed sibling would
  not have been enough:

  | Group | Blobs | `CB2` | `CB12` | `CB2` registers read |
  |---|---|---|---|---|
  | `dir_base`         | a9435eca | `[3]` | `[30]` | 0,1,2 |
  | `dir_spec`         | 039c8935, 28858d7b | `[3]` | `[31]` | 0,1,2 |
  | `dir_spec_ambient` | 477c3e1e, 987c4e79 | `[9]` | `[31]` | 0,1,2,6,7,8 |
  | `omni`             | 12d92cd3, 9f44ba67, ea2537f5, 8765cebe, b4337a89, fcabd749 | `[4]` | `[30]` | 0,1,2,3 |

  The shadowed siblings are materially different and cannot host these: the shadowed
  `DIRSPLITS=2` directional declares `CB2[25]` plus `t5`/`s5`, and the shadowed point
  family declares `CB2[15]` plus `t7`/`s7`. Sizes are not authored directly, and
  `fxc` records each one twice from different inputs: the SHEX
  `dcl_constantbuffer` size comes from the highest register the body **reads**,
  plus one, while the RDEF reflection size comes from what the source
  **declares**. Both must match the native blob, so every trailing constant is
  declared under the same condition that reads it and the two sizes fall out
  together. `dir_spec_ambient` leaves registers 3, 4 and 5 unread, so its `CB2` has a
  hole.

  Checking only the instruction stream is not sufficient, and this was a real
  defect rather than a hypothetical one. A trailing member declared
  unconditionally but read by only some permutations leaves the SHEX size
  untouched and still widens the reflected size, where it appears merely as
  `[unused]`. That is invisible to a `dcl_`-only check: the base directional and
  all six `POINTOMNI` permutations reflected `CB12` as 31 registers against a
  native 30, which a host reading reflection rejects as a contract mismatch. The
  admission gate therefore compares the reflected size as a second, independent
  opinion on the size the manifest already pins from SHEX.

  That read-set table also settles what `DIRSPLITS=2` means here, and it is worth
  stating plainly because the name invites the wrong reading: for these eleven it is the
  **decoder baseline, not an active cascade axis**, and this layer does not own
  two-cascade behaviour. The highest `CB2` register any of the eleven reads is 8. None
  reads a split-distance, cascade-projection, or shadow world-scale or filter register,
  and those constants are not merely unread - `SplitDistances`, `FadeDistances`,
  `ShadowMapProj` and the rest are `absent` from all eleven constant tables, so no
  register is allocated to them at all. The `dir_spec_ambient` rows at `cb2[6..8]` are
  a single `DirectionalAmbient` constant of `register_count` 3, not a split/fade pair -
  a distinction worth checking rather than assuming, since slot 7 in the full constant
  layout is named `SplitDistances` and the per-blob table is compacted. The macro is
  still carried and still guarded, because it is a native axis that is never assumed,
  but it is deliberately absent from the source, manifest and gate names: what this
  layer owns is unshadowed lighting, not a cascade count and not a shadow selection.

  `DIRECTIONAL` and `POINTOMNI` are separately reconstructed bodies in one file, even
  though their resource contracts match, because the disassembly differs in three  ways. `DIRECTIONAL` + `SPECULAR` reads `cb12[30].y` as a Schlick gloss term and uses
  it twice, to scale the specular exponent and to scale the specular output; that
  single read is what raises those blobs to `CB12[31]`. `POINTOMNI` + `SPECULAR` does
  none of it and stays at `CB12[30]`. `POINTOMNI` reads `cb2[3]` `LightAttenuation`
  and treats `cb2[1].xyz` as a light position with its radius in `.w`, computing
  attenuation before any G-buffer sample and returning two zeroed targets early when
  it falls below `0.001`; no directional blob reads `cb2[3]` at all. Directional
  composition adds a premultiplied-albedo backface wrap, a depth-scaled forward blend
  and an albedo tint, for which the point blobs have no instructions.

  Because "these blocks look the same" is not evidence, the reuse question was settled
  by comparing native ASM against the shadowed `DIRSPLITS=2` family on two controlled
  pairs that differ only by `SHADOW` and its filter - 039c8935 against 0fd35e4a (180 vs
  222 instructions) and 9f44ba67 against 2fe442f1 (179 vs 240). Three grades came out
  of it, and only the first is an identity claim:

  | grade | directional pair | point pair | what it covers |
  |---|---|---|---|
  | identical, verified on instruction text | leading 26 | leading 38 | VPOS offset, depth fetch, view-position reconstruction |
  | equal only after renaming registers | next 10 | next 18 | G-buffer samples and normal decode |
  | different | remainder | remainder | everything from the shadow entry onward |

  The middle grade is the one worth being strict about. Those instructions match
  opcode-for-opcode but allocate different registers (`r2`/`r3` against `r3`/`r4`), so
  they are similar, not identical, and no identity or reuse is claimed for them. The
  bodies diverge for real immediately after: the shadowed directional enters cascade
  selection at `lt ..., cb2[9].w` where this family goes straight to lighting, and the
  shadowed point builds a shadow projection with `dp4` against `cb2[11]`/`cb2[12]`
  where this family has no such matrix at all. So the BRDF, normal-decode and lighting
  bodies here are independently reconstructed, and nothing is factored into a shared
  `.hlsli` on similarity alone.

  Both risk axes have exact constant read-count pins rather than exemptions.
  `IGNOREROUGHNESS` is measured on two pairs - 039c8935/28858d7b with no
  `AMBIENT`, and 477c3e1e/987c4e79 with it. Counting instructions only
  (excluding the 15 `dcl_` lines), the pairs run 181 -> 151 and 208 -> 179.
  Native 477c3e1e retains `(3 - matSample.x)`, while its IGNOREROUGHNESS target
  987c4e79 uses the fixed square. The AMBIENT pair's exact -900 B /
  -29-instruction reduction therefore covers visibility and rim removal plus
  that fixed-square substitution.

  What moves, and what does not, is the whole point:

  | register | 039c8935 -> 28858d7b | 477c3e1e -> 987c4e79 |
  |---|---|---|
  | `cb2[1]`  | 5 -> 3 | 5 -> 3 |
  | `cb2[2]`  | 6 -> 5 | 6 -> 5 |
  | `cb12[28]` | 4 -> 4 | 4 -> 4 |
  | `cb12[29]` | 2 -> 2 | 2 -> 2 |
  | `cb12[30]` | 1 -> 1 | 1 -> 1 |
  | `cb2[6..8]` | n/a | 2 -> 2 each |

  The point body supplies two further native pairs without changing its math:
  12d92cd3/ea2537f5 runs 142 -> 107 instructions and
  9f44ba67/8765cebe runs 196 -> 167. `IGNOREROUGHNESS` leaves `cb2[0]`, `cb2[1]`,
  `cb2[3]` and every `cb12[20..29]` read unchanged; only `cb2[2]` drops 2 -> 1
  or 4 -> 3. Those counts equal the corresponding `IGNORERIM` rows, while the
  lower native instruction counts also include the roughness-visibility deletion.

  `IGNOREROUGHNESS` removes the roughness visibility geometry, collapsing the
  default branch's diffuse to a plain N·L, and the rim term. The AMBIENT target
  also replaces its roughness-dependent exponent with the fixed square. It does
  **not** touch the material-code-1 hair specular path: `cb12[28]`
  `HairSpecParams` and `cb12[29]` `HairSpecShift` hold at 4 and 2 across both
  pairs, and both Kajiya-Kay `sincos` lobes survive. Nor does it touch the
  ambient gradient: `cb2[6..8]` hold at 2 reads each. Native removes one `log`
  and two `exp` instructions across the ambient exponent and rim, plus one
  `sqrt`; the fixed-square substitution is texture-derived and changes no
  constant read count.

  The no-`AMBIENT` pair explains the shadowed family's two fewer `cb2[1]` reads
  and one fewer `cb2[2]` read, not its complete math. The unshadowed source now
  reconstructs the AMBIENT distinction directly: 477c3e1e retains the exponent,
  while only its IGNOREROUGHNESS target 987c4e79 uses the fixed square. Direct
  same-fixture producer WARP evidence measured zero divergence for the fixed-square
  candidate and 10,747/4,109 divergent pixels for the retained-exponent candidate.
  Shadowed DS2 and DS3 use the fixed square and are producer-proven. `IGNORERIM` is measured on
  12d92cd3/b4337a89 and 9f44ba67/fcabd749: it removes only the rim term, for exactly
  one fewer `cb2[2]` read. Both axes retain exact read-count coverage:
  `scope.count_exemption_axis` is `null` in both manifests and every one of the
  eleven entries is held to exact per-register read-counts.

  The `CB12` size is the one pin this layer originally got wrong, which is worth
  recording because the failure was silent. `cb12[30]` was declared for every
  permutation and read only by the specular directional bodies, so the SHEX
  `dcl_constantbuffer` size stayed correct at 30 while reflection reported 31
  with the member marked `[unused]`. The base directional blob and all six
  `POINTOMNI` blobs were therefore rejected by the producer harness as a
  constant-buffer contract mismatch even though the gate here was green. The
  member is now declared under the same condition that reads it, and the gate
  compares reflected sizes as well as declared ones, so this class of defect
  cannot pass again.

  `scripts/shaders/verify-native-abi-admission.ps1` re-measures the layer against two
  evidence manifests - `unshadowed-directional-native-abi.json` (CTest
  `UnshadowedDirectionalAdmission`) and
  `unshadowed-pointomni-native-abi.json` (CTest `UnshadowedPointOmniAdmission`).
  Two manifests rather than one, because the verifier supports a single
  `count_exemption_axis` per manifest and the two families carry different risk axes.
  Between them the gates assert that 27 malformed or out-of-scope macro sets still
  refuse to compile - any `FILTER_*` without `SHADOW`, `SHADOW` or `SHADOW_ONLY`
  present, `HALFOMNI`, `GOBOPROJECTION`, `SPOT`/`POINTSPOT`, mixed or missing light
  kind, `DIRSPLITS` absent or not 2, missing `RGBSPEC`, `DIRECTIONAL`+`IGNORERIM`,
  `DIRECTIONAL`+`AMBIENT` without `SPECULAR`, `DIRECTIONAL`+`IGNOREROUGHNESS` without
  `SPECULAR`, `POINTOMNI` crossed with `AMBIENT`, and the non-native
  `POINTOMNI+IGNOREROUGHNESS+IGNORERIM` overlap. The point admission denominator moves
  from 4/4 to 6/6: the ten unrelated prior reject controls are unchanged, and the
  reclassified target control is replaced by the overlap guard, so the reject list
  remains 11/11. `POINTOMNI+IGNOREROUGHNESS` without GOBOPROJECTION is native in exactly
  the two newly admitted macro sets; GOBOPROJECTION still selects the distinct t7/s7
  family and remains rejected before the point-specific guards. The reject set is
  derived from the full 166-blob enumeration, so a native set belonging to another
  layer is named as such instead of being called malformed. Each manifest's
  `compile_only` list holds the
  *sibling* family's native macro sets, proving the guards lock out the other family
  without locking out anything legal.

  This is an ABI claim, not SHEX equality and not execution equivalence. SHEX identity
  was attempted and not reached; the residual instruction deltas run from -1 to +2.
  Execution proof stays with the producer oracle.

  The producer currently routes the original nine rows to this file. This consumer
  admission adds ea2537f5 and 8765cebe without changing their bodies; both remain
  `execution_unproven` until the producer target map uses this source and reruns the
  existing `pointomni-unshadowed` profile. All eleven compile here under
  `/T ps_5_0 /O3 /E main`, but compilation and ABI equality add no execution proof.

  The routing family is exactly eleven, but the broader predicate
  "`DIRSPLITS=2` and no `SHADOW`" also includes GOBOPROJECTION, ATTENUATION_ONLY,
  SPOT and no-light-kind contracts. Those keep their distinct declarations and are
  not admitted by this manifest. The producer enumeration owns that wider
  denominator; this consumer scope is the exact `t0..t3`/`s0..s3` resource family.

* **`bsdf_light_deferred_gobo.hlsl`** - **native ABI admission, Wave 1 C1**.
  The five unshadowed `POINTOMNI` cookie blobs are a separate resource-contract
  family: in addition to the four G-buffer resources they declare `t7`/`s7` for
  the dual-paraboloid light cookie, with `CB12[30]` and `CB2[15]`. Wave 1 admits
  the four `SPECULAR` × `IGNORERIM` cells and keeps the two
  `IGNOREROUGHNESS` cells compile-only until their different visibility/rim body
  is reconstructed. `UnshadowedPointOmniGoboAdmission` pins the native ABI and
  constant reads, rejects the shadowed, foreign-light, filter, ambient,
  attenuation, half-omni and blend-split neighbors, and records the SHA-256 of
  the pinned source it was extracted from as provenance rather than fidelity.

* **`bsdf_light_deferred_dirsplits3.hlsl`** - **native ABI equal, 27/27,
  read-counts exact on every entry**.
  The native full-BRDF `DIRECTIONAL` + `SHADOW` family at `DIRSPLITS=3`, the
  three-cascade sibling of the `DIRSPLITS=2` layer. The denominator is fixed and
  closed: the AE 1.11.221 archive carries exactly 27 blobs with
  `DIRECTIONAL + DIRSPLITS=3 + RGBSPEC + SHADOW + SPECULAR`, and all 27 are
  admitted, so the ratio cannot be improved by picking a friendlier subset. They
  vary over the same four axes - `FILTER_*` crossed with `AMBIENT` (14),
  `BLENDSPLIT` (16) and `IGNOREROUGHNESS` (6). `IGNORERIM` occurs zero times and
  is rejected rather than admitted-and-ignored.

  The split count is not a parameter of the `DIRSPLITS=2` source, it is a
  different constant buffer, which is why this is a sibling file: nine
  projection rows fill `CB2[11..19]` where two cascades leave `[17..19]` as
  holes, and `ShadowWorldScale` is three vectors at `CB2[21..23]` where two
  cascades have two and a hole. `CB2[25]` and `CB12[31]`, both immediateIndexed,
  hold across all 27 - every cascade call passes literal rows, a literal slice
  and a literal world-scale register, so nothing indexes `CB2` at runtime.

  The filter axis moves the resource contract and nothing else does:

  | Filter | Blobs | Shadow map declarations |
  |---|---|---|
  | (none)                | 3 | `t4`/`s4` mode_default (raw tap, compare in the shader) |
  | `FILTER_PCF1`         | 4 | `t5`/`s5` mode_comparison |
  | `FILTER_PCF9`         | 6 | `t5`/`s5` mode_comparison |
  | `FILTER_PCSS`         | 6 | `t4`/`s4` mode_default **and** `t5`/`s5` mode_comparison |
  | `FILTER_PCSSPOISSON`  | 2 | `t4`/`s4` mode_default **and** `t5`/`s5` mode_comparison |
  | `FILTER_POISSON`      | 6 | `t5`/`s5` mode_comparison |

  That is 3 in `raw_t4_s4`, 16 in `cmp_t5_s5` and 8 in `raw_t4_cmp_t5`.

  `FILTER_PCSSPOISSON` is the branch `DIRSPLITS=2` does not have, and it is not
  PCSS with a Poisson tail bolted on: the 5x5 raw blocker search and the
  world-range remap are PCSS's, but the kernel radius is `penumbra * searchStep.x`
  rather than `CB2[20]`, there is no centre-lit seed and no depth bias, and the
  16 comparison taps are averaged over a fixed 1/16. It therefore reads
  `CB2[20]` zero times while reading each world-scale register six times, the
  same six as PCSS.

  Per-cascade constant reads are what the gate actually pins, and they are
  per-cascade on purpose - nothing is hoisted out of the three calls. `CB2[20]`
  `ShadowSampleParam` is read three times under `FILTER_PCF9` and
  `FILTER_POISSON` and not at all otherwise; each of `CB2[21..23]` is read twice
  per cascade under `FILTER_POISSON` (the reciprocal world range for the bias)
  and six times per cascade under `FILTER_PCSS` and `FILTER_PCSSPOISSON` (texel
  step, world range, receiver, blocker, and the near-plane test). `CB2[23]`
  exists only on the `POISSON`/`PCSS`/`PCSSPOISSON` rows and on all of them.
  Each of `CB2[17..19]`, the third cascade's projection rows, is read exactly
  once on every one of the 27. `FILTER_POISSON`'s bias scale is per cascade:
  0.275, 1.0, 1.0.

  `CB2[10]` `FadeDistances` carries two transition pairs at three splits, `.x`/`.y`
  for cascade 0 to 1 and `.z`/`.w` for cascade 1 to 2. Cascade 0 is active below
  `.y`, cascade 1 between `.x` and `.w`, cascade 2 above `.z`. Under `BLENDSPLIT`
  the two smoothsteps run `.x -> .y` and `.z -> .w`, giving the five-region
  partition s0, blend01, s1, blend12, s2, and the register is read 5 times.
  Without `BLENDSPLIT` it is read twice, `CB2[9]` `SplitDistances.w` appears as
  the single outer gate wrapping all cascade selection *and* the distance fade,
  the lower overlap `[.x, .y)` resolves to unshadowed exactly as it does at two
  splits, and the upper overlap `[.z, .w)` belongs to cascade 2. Those two
  read-counts are the whole `BLENDSPLIT` signature, and they only come out right
  because the comparisons are written so `fxc` packs them into the two vector
  compares native emits.

  `IGNOREROUGHNESS` is reconstructed in both shadowed directional layers. The
  controlled `AMBIENT`-free pair in `bsdf_light_deferred_unshadowed.hlsl`
  localises the roughness-driven visibility geometry and rim deletions.
  Shadowed `AMBIENT` disassembly adds one more delta: the roughness-dependent
  exponent becomes an exact fixed square while the gradient and material.y
  paths remain. Each of the six `DIRSPLITS=3` blobs differs from its twin by
  exactly `cb2[1]` -2 and `cb2[2]` -1. Both shadowed manifests omit
  `scope.count_exemption_axis` and hold every entry to exact per-register
  read-counts.

  The gate carries a fourth pin this layer needed and the earlier ones did not.
  Eight of the 27 - the `POISSON` and `PCSSPOISSON` rows - declare a 1000-row
  immediate constant buffer, and the other 19 declare none, so
  `immediate_constant_vectors` is pinned per entry and the verifier counts the
  `float4` rows in the listing's `dcl_immediateConstantBuffer` block. The pin is
  all-or-none across a manifest: a family that measured it cannot drop it from
  the one entry it would have caught. It is a *declaration* count only - it says
  the kernel table is present at the native size, not that the 16 consumed
  entries are the native values, which is what
  `shadow_poisson_kernel.hlsli` records separately. Manifests written before the
  pin existed simply omit it and behave exactly as before.

  `scripts/shaders/verify-native-abi-admission.ps1` (CTest
  `DirSplits3DirectionalAdmission`) re-measures all 27 against
  `dirsplits3-native-abi.json` and fails closed. The pinned values were measured
  from the game bytecode with `fxc /nologo /dumpbin` and were not revised against
  this repository's output. The gate also asserts that 17 single-mutation macro
  sets still refuse to compile - `DIRSPLITS` of 1/2/4/absent, three `FILTER_*`
  overlaps including one with `FILTER_PCSSPOISSON`, `SHADOW_ONLY`, a missing
  `DIRECTIONAL`/`SHADOW`/`SPECULAR`/`RGBSPEC`, `DIRECTIONAL` crossed with
  `POINTOMNI`/`POINTSPOT`/`SPOT`/`HALFOMNI`, and `IGNORERIM` - each of which is
  otherwise a valid `DIRSPLITS=3` set, so every guard is shown to fire for its
  own reason rather than incidentally. The 21 remaining cells of the
  6 x `AMBIENT` x `BLENDSPLIT` x `IGNOREROUGHNESS` grid have no archive blob and
  are checked for compilation only, so the guards cannot over-reach.

  What this claims: these 27 macro sets compile, and what they compile to
  declares the same ABI, performs the same static constant reads with the same
  counts, and declares the same immediate constant-buffer size as the blob each
  came from, over a fixed 27-blob denominator. What it does not claim: nothing
  here is a claim about execution, about visual fidelity, or about SHEX/DXBC
  identity - the BRDF core is a structural reconstruction and its instruction
  stream is expected to differ. No shader in this family is created or used at
  runtime by this repository. The archive is one runtime's, so this is neither an
  OG nor an NG claim. Producer execution status is carried per entry and is 16
  `execution_failed` and 11 `execution_unproven`, with zero PASS; execution proof
  stays with the producer oracle in the sibling `fallout4-re`.

## Workflow

Game-bytecode extraction, reconstruction evidence, and executable comparison
live in `northaxosky/fallout4-re`. This repository retains a consumer-local
pinned `scripts/shaders/shader-fidelity-conformance.json` from the last valid
attestation and does not carry the corpus or RE tools.

`ShaderRoundtrip` validates the eight pinned variants against that manifest.
`ShaderCompile` also compiles the three shipping permutations without
native fidelity evidence: deferred composite and the two Screen Space Shadows
directional variants. Replacing three machine-specific exec-diff tests with
these clean-clone gates strengthens the suite: conformance is no longer
self-rebaselinable, while those registered variants still receive compile
coverage.

The consumer-local manifest must never be hand-edited. The producer deleted
its conformance artifact in `fallout4-re` commit `be8126d4`, then restored it
in `e8a81748` and has republished it on every subsequent wave from a full
authoritative WARP execution-diff PASS across all eight targets.
`ShaderRoundtrip` is therefore a live conformance gate again: a pass means the
shipping HLSL still compiles to bytecode proven numerically equal to the
game's own shader. Refresh it only by copying the producer artifact
byte-for-byte after a new authoritative PASS.

## Permutation coverage

FO4's Light raw-technique rules span about 20 macro axes, including `SHADOW`,
`SPECULAR`, `ATTENUATION_ONLY`, `OVERDRAW`, `GOBOPROJECTION`, `HALFOMNI`,
`IGNOREROUGHNESS`, `IGNORERIM`, `AMBIENT`, `SHADOW_ONLY`, `BLENDSPLIT`,
`CHARACTER_LIGHT`, the exclusive PCF1→PCF9→POISSON→PCSS→PCSSPOISSON filter
chain, and three-way `DIRSPLITS`. They produce 166 distinct Light blobs (306
route occurrences) and 78 Composite blobs (180 occurrences) on AE. Those censuses
are fixed denominators and must never shrink to make a report greener.

**Strategy: reconstruct every archive permutation, not only the ones we inject
into.** A permutation that exists in the archive gets reconstructed whether or
not the game is ever observed using it; unused `#ifdef` branches cost nothing at
runtime because permutations compile on demand. Runtime observation is
validation and prioritisation, never an input to reconstruction.

Measured by the producer's WARP execution-diff oracle:

| Family | Census | Numerically proven |
|---|---|---|
| `BSDFLightShader` | 166 blobs / 306 routes | **125** |
| `BSDFCompositeShader` | 78 blobs / 180 routes | 0 (workstream in progress) |

`bsdf_light_deferred.hlsl` models `LIGHT_TYPE`, `AMBIENT_IBL_IN_LIGHT`,
`POINTOMNI`+`SHADOW`, `HALFOMNI`, `GOBOPROJECTION` and the `FILTER_*` axis;
sibling files carry the `DIRSPLITS=2`, `DIRSPLITS=3`, `SHADOW_ONLY` and
unshadowed families. Remaining light work, largest cluster first: 17 blobs
carrying `IGNOREROUGHNESS`, 6 `DIRSPLITS=1` directional shadowed, 3 unshadowed
`POINTOMNI`+`GOBOPROJECTION`, plus 11 execution-unproven, 3 compile failures and
1 unresolved blob.

**Reconstruction is not delivery.** `src/Render/ShaderInjection.cpp` registers
only `LIGHT_TYPE=1`, `LIGHT_TYPE=2`, and `LIGHT_TYPE=1` with
`AMBIENT_IBL_IN_LIGHT=1`, so an injected wetness, SSGI, or SSS effect appears
only when the engine draws through a registered permutation; stock shaders run
on every other one and the injected effect silently disappears. Numerical
fidelity and in-game execution are independent — a correct-but-never-invoked
shader looks identical on screen to a correct-and-running one, because the stock
shader was also fine. Only non-zero replacement counters distinguish them.

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

Shadowed point lights are reconstructed and numerically proven — all 31
`POINTOMNI`+`SHADOW` blobs, including the `HALFOMNI` and `GOBOPROJECTION`
variants. Spot lights, fog, the `IGNOREROUGHNESS` axis and per-material prepass
permutation axes remain unmapped; they are queued as follow-up work. The
`BSDFCompositeShader` family (78 blobs) is a separate active workstream with no
fidelity evidence yet.

## License

These files are derivative reverse-engineering of Bethesda's compiled
shader binaries. They are licensed under this repo's terms (`LICENSE`
and `EXCEPTIONS.md`); see each file's header for attribution.
