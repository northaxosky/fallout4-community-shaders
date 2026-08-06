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
| `bsdf_light_deferred_dirsplits2.hlsl` | BSDFLightShader deferred PS, native full-BRDF `DIRECTIONAL`+`SHADOW` family at `DIRSPLITS=2`; carries the `FILTER_*` axis (none / PCF1 / PCF9 / PCSS / POISSON) crossed with `AMBIENT` × `BLENDSPLIT` × `IGNOREROUGHNESS` | reads `t0..t3` (G-buffer aliases + main depth), cascade shadow Texture2DArray at `t4` (raw) and/or `t5` (comparison); writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | same host as the directional path above | **native-abi-equal, 29/29** |
| `bsdf_light_deferred_unshadowed.hlsl` | BSDFLightShader deferred PS, the native **unshadowed** `DIRSPLITS=2` families - `DIRECTIONAL` (5 blobs) and `POINTOMNI` (4 blobs), no `SHADOW` macro, so no shadow resource at all | reads `t0..t3` only (G-buffer aliases + main depth) with `s0..s3` mode_default; writes `kDiffuseBuffer=58` + `kSpecularBuffer=59` | same hosts as the directional and point paths above | **native-abi-equal, 9/9 (read-counts exact, no axis exempt)** |

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

  **POINTOMNI+SHADOW admission** (`-D LIGHT_TYPE=3 -D POINTOMNI=1 -D SHADOW=1`).
  A producer audit of the 73 execution-unproven blobs found 30 native
  POINTOMNI+SHADOW records whose declared ABI and FXP constant tables are
  identical to the already-proven POINTSPOT profiles. An internal selector,
  `FO4_PROJECTED_SHADOW_FAMILY`, routes both families to the same
  projected-shadow branch, so POINTOMNI is admitted without being rewritten
  into POINTSPOT and without either family's native macro set being touched.
  All 30 compile and are contract-equal to their corpus blob — CB sizes and
  indexing mode, SRV slots and types, sampler slots and modes, and the IO
  signature all match: `CB12[30]` + `CB2[21]` immediateIndexed, `t0..t3` with
  `s0..s3` mode_default, the shadow map at `t5`/`s5` mode_comparison under
  `FILTER_PCF1` (9) / `FILTER_PCF9` (10) / `FILTER_POISSON` (8) or at `t4`/`s4`
  mode_default in the 3 unfiltered records - 9 + 10 + 8 + 3 = 30, this family's
  own count, not to be read against the `DIRSPLITS=2` figures below - plus
  `t7`/`s7` in the 10 `GOBOPROJECTION` records.

  This is an ABI claim only. The body is the POINTSPOT reconstruction, so
  execution is expected to diverge for an omni light, and `HALFOMNI` — carried
  by 12 of the 30 and by no other blob in the set — is deliberately left
  defined and unreconstructed rather than rejected or erased. POINTOMNI
  *without* `SHADOW` is a different ABI and stays on `LIGHT_TYPE=2`; the
  `LIGHT_TYPE=3` guards reject it explicitly.

  `scripts/shaders/verify-pointomni-admission.ps1` (CTest
  `PointOmniShadowAdmission`) re-measures all 30 and fails closed. It also
  asserts that 15 malformed macro sets still refuse to compile — POINTOMNI
  without `SHADOW`, POINTOMNI with `SPOT`/`POINTSPOT`/`ATTENUATION_ONLY`, two
  `FILTER_*` at once, `FILTER_PCSS`/`FILTER_PCSSPOISSON` anywhere on this path
  (all 15 PCSS and all 3 PCSSPOISSON blobs in the archive are DIRECTIONAL),
  `HALFOMNI` without POINTOMNI, and POINTOMNI+SHADOW misrouted to
  `LIGHT_TYPE=2` — and that the 9 native POINTOMNI-without-SHADOW macro sets
  still compile on `LIGHT_TYPE=2`, so the guards cannot over-reach.
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
  and are not reconstructed here.
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
  it would be a fidelity claim with no evidence behind it. `IGNOREROUGHNESS`
  follows the `HALFOMNI` precedent - 11 of the 29 carry it and it does not move
  the contract, but it does change the ambient-specular exponent path, so it is
  admitted and left unreconstructed rather than erased or rejected.

  `scripts/shaders/verify-native-abi-admission.ps1` (CTest
  `DirSplits2DirectionalAdmission`) re-measures all 29 against
  `dirsplits2-native-abi.json` and fails closed. Each entry pins three things
  taken from the game bytecode - the blob's declaration set, the set of
  constant-buffer registers its body reads, and how many times it reads each -
  so the gate cannot bless this repository's output.

  Those three pins are progressively finer. The read-set separates macro sets
  the declarations cannot: a `BLENDSPLIT` blob reads `cb2[6..8]` and never
  `cb2[9]`, while its plain counterpart reads `cb2[9]` as `SplitDistances`. The
  read-count is finer still, and is where the `IGNOREROUGHNESS` gap becomes
  measurable rather than merely stated: counts are exact on all 18 entries whose
  bodies are reconstructed, and differ on exactly the 11 `IGNOREROUGHNESS`
  entries - always the same two registers, `cb2[1]` and `cb2[2]`, which the
  unreconstructed ambient-specular exponent path would have read again. The
  divergent set and the `IGNOREROUGHNESS` set are identical with nothing on
  either side, which is why the exemption is declared as one named axis in
  `scope.count_exemption_axis` and re-derived by the verifier: an entry may skip
  the count check only by carrying that macro and giving a reason, so the
  exemption cannot be quietly widened to hide a real divergence.


  The gate also asserts that 16 malformed or out-of-scope macro sets still
  refuse to compile - `FILTER_PCSSPOISSON`, `DIRSPLITS` of 1/3/4/absent, two
  `FILTER_*` at once, `SHADOW_ONLY`, a missing
  `SHADOW`/`SPECULAR`/`RGBSPEC`/`DIRECTIONAL`, and `DIRECTIONAL` crossed with
  `POINTOMNI`/`POINTSPOT`/`SPOT`/`HALFOMNI` - and that the 11 axis combinations
  that are legal natively but happen to have no archive blob still compile, so
  the guards cannot over-reach.

  This is an ABI claim, not SHEX equality. The declarations, constant read-sets
  and - on the 18 reconstructed bodies - constant read-counts are measured; the
  BRDF core is a structural reconstruction, and execution proof stays with the
  producer oracle. The verifier is family-agnostic and driven entirely from its
  manifest, so the `DIRSPLITS=3` layer needs an evidence file and a test
  registration rather than a third copy of the script.

* **`bsdf_light_deferred_unshadowed.hlsl`** - **native ABI equal, 9/9, read-counts
  exact on every entry**.
  The native **unshadowed** `DIRSPLITS=2` layer. Nine archive blobs carry
  `DIRSPLITS=2` and no `SHADOW` macro, so their contract has no shadow texture and
  no shadow sampler: five `DIRECTIONAL` and four `POINTOMNI`. `SHADOW` is the outer
  ABI selector here. When it is absent there is no shadow resource; only when it is
  present does `FILTER_*` choose raw `t4`/`s4`, comparison `t5`/`s5`, or both.

  All nine declare `t0..t3` as `texture2d` with `s0..s3` mode_default, the same two
  input semantics and the same two MRT outputs, and all read `CB12[20..29]`. They are
  **not** one contract, though - the constant-buffer sizes split them into four
  groups, which is why removing the shadow declarations from a shadowed sibling would
  not have been enough:

  | Group | Blobs | `CB2` | `CB12` | `CB2` registers read |
  |---|---|---|---|---|
  | `dir_base`         | a9435eca | `[3]` | `[30]` | 0,1,2 |
  | `dir_spec`         | 039c8935, 28858d7b | `[3]` | `[31]` | 0,1,2 |
  | `dir_spec_ambient` | 477c3e1e, 987c4e79 | `[9]` | `[31]` | 0,1,2,6,7,8 |
  | `omni`             | 12d92cd3, 9f44ba67, b4337a89, fcabd749 | `[4]` | `[30]` | 0,1,2,3 |

  The shadowed siblings are materially different and cannot host these: the shadowed
  `DIRSPLITS=2` directional declares `CB2[25]` plus `t5`/`s5`, and the shadowed point
  family declares `CB2[15]` plus `t7`/`s7`. Sizes are not authored directly - `fxc`
  sets a declared constant-buffer size to the highest register the body reads plus
  one, so each size above falls out of which registers that permutation actually
  touches. `dir_spec_ambient` leaves registers 3, 4 and 5 unread, so its `CB2` has a
  hole.

  `DIRECTIONAL` and `POINTOMNI` are separately reconstructed bodies in one file, even
  though their resource contracts match, because the disassembly differs in three
  ways. `DIRECTIONAL` + `SPECULAR` reads `cb12[30].y` as a Schlick gloss term and uses
  it twice, to scale the specular exponent and to scale the specular output; that
  single read is what raises those blobs to `CB12[31]`. `POINTOMNI` + `SPECULAR` does
  none of it and stays at `CB12[30]`. `POINTOMNI` reads `cb2[3]` `LightAttenuation`
  and treats `cb2[1].xyz` as a light position with its radius in `.w`, computing
  attenuation before any G-buffer sample and returning two zeroed targets early when
  it falls below `0.001`; no directional blob reads `cb2[3]` at all. Directional
  composition adds a premultiplied-albedo backface wrap, a depth-scaled forward blend
  and an albedo tint, for which the point blobs have no instructions.

  Both risk axes are adjudicated from controlled, semantically active pairs inside
  this layer, so neither is exempted. `IGNOREROUGHNESS` is measured on two pairs
  (039c8935/28858d7b at 196/166 native instructions, and 477c3e1e/987c4e79 at
  223/194): it removes the roughness visibility geometry, collapsing the default
  branch's diffuse to a plain N·L, **and** it removes the whole rim/ambient term. That
  is the first `IGNOREROUGHNESS` pair with no `AMBIENT` present, and it explains the
  shadowed `DIRSPLITS=2` family's previously unexplained deltas exactly - two fewer
  `cb2[1]` reads and one fewer `cb2[2]` read. `IGNORERIM` is measured on
  12d92cd3/b4337a89 and 9f44ba67/fcabd749: it removes only the rim term, for exactly
  one fewer `cb2[2]` read. Both are reconstructed, so
  `scope.count_exemption_axis` is `null` in both manifests and every one of the nine
  entries is held to exact per-register read-counts.

  `scripts/shaders/verify-native-abi-admission.ps1` re-measures the layer against two
  evidence manifests - `unshadowed-ds2-directional-native-abi.json` (CTest
  `UnshadowedDs2DirectionalAdmission`) and
  `unshadowed-ds2-pointomni-native-abi.json` (CTest `UnshadowedDs2PointOmniAdmission`).
  Two manifests rather than one, because the verifier supports a single
  `count_exemption_axis` per manifest and the two families carry different risk axes.
  Between them the gates assert that 26 malformed or out-of-scope macro sets still
  refuse to compile - any `FILTER_*` without `SHADOW`, `SHADOW` or `SHADOW_ONLY`
  present, `HALFOMNI`, `GOBOPROJECTION`, `SPOT`/`POINTSPOT`, mixed or missing light
  kind, `DIRSPLITS` absent or not 2, missing `RGBSPEC`, `DIRECTIONAL`+`IGNORERIM`,
  `DIRECTIONAL`+`AMBIENT` without `SPECULAR`, and `POINTOMNI` crossed with `AMBIENT`
  or `IGNOREROUGHNESS`. The reject set is derived from the full 166-blob enumeration
  rather than from the nine, so the guards neither over-reach nor under-reach; where a
  rejected set does exist natively but belongs to another layer, the manifest says so
  instead of calling it malformed. Each manifest's `compile_only` list holds the
  *sibling* family's native macro sets, proving the guards lock out the other family
  without locking out anything legal.

  This is an ABI claim, not SHEX equality and not execution equivalence. SHEX identity
  was attempted and not reached; the residual instruction deltas run from -1 to +3.
  Execution proof stays with the producer oracle.

  The producer has since routed to this file, and the two sides agree without either
  having been derived from the other. Its target contracts map exactly these nine
  blobs to `bsdf_light_deferred_unshadowed.hlsl`, emitting precisely the nine macro
  sets pinned above and no `LIGHT_TYPE` - the consolidated file's adapter macro, which
  the guards here reject. All nine compile from this source under the producer's own
  flags (`/T ps_5_0 /O3 /E main`). Their strict execution profiles are
  `directional-unshadowed` for the five directional rows and `pointomni-unshadowed`
  for the four point rows; neither profile's predicates read a constant register
  outside these `CB2` sizes, and `pointomni-unshadowed` additionally requires the
  `atten <= 0.001` cull and distinct `cb12[20..27]` reprojection banks to be
  exercised, which are the early-out and the near/far partition reconstructed here.

  The layer is nine and not fourteen because of one boundary worth stating. Five more
  archive blobs are unshadowed `POINTOMNI` at `DIRSPLITS=2` but also carry
  `GOBOPROJECTION` (`a65b5952`, `9969e800`, `fa6948ba`, `f33e32f9`, `d3331d19`). They
  keep the distinct `t7` light-cookie ABI, so they are a different contract and stay
  on the consolidated source; this file's guards reject that combination. Being
  unshadowed is not on its own sufficient to belong here - the whole resource
  contract has to match.

  Routing does not add execution proof. It makes these nine *measurable*, which is the
  point; whether they then pass is a separate question the oracle answers.

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
its conformance artifact in `fallout4-re` commit `fdddc41a`, and publication
of conformance, `native-shader-targets.json`, and
`shader-exec-contracts.json` is suspended until a new authoritative
all-target execution proof passes. `ShaderRoundtrip` therefore remains a
pinned-hash regression gate: it proves that shipping HLSL still compiles to
the bytes produced when the attestation was last valid, not current
producer-attested fidelity.

## Permutation coverage

FO4's Light raw-technique rules span about 20 macro axes, including `SHADOW`,
`SPECULAR`, `ATTENUATION_ONLY`, `OVERDRAW`, `GOBOPROJECTION`, `HALFOMNI`,
`IGNOREROUGHNESS`, `IGNORERIM`, `AMBIENT`, `SHADOW_ONLY`, `BLENDSPLIT`,
`CHARACTER_LIGHT`, the exclusive PCF1→PCF9→POISSON→PCSS→PCSSPOISSON filter
chain, and three-way `DIRSPLITS`. They produce 166 distinct Light blobs and 78
Composite blobs on AE. The reconstructed
`bsdf_light_deferred.hlsl` currently models only `LIGHT_TYPE` and
`AMBIENT_IBL_IN_LIGHT`; `src/Render/ShaderInjection.cpp` registers only
`LIGHT_TYPE=1`, `LIGHT_TYPE=2`, and `LIGHT_TYPE=1` with
`AMBIENT_IBL_IN_LIGHT=1`, while `LIGHT_TYPE_SPOT` remains a stub. An injected
wetness, SSGI, or SSS effect therefore appears only when the engine draws
through a built permutation; stock shaders run on every other permutation,
and the injected effect silently disappears.

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
