# Shader 2122 / DeferredComposite — campaign analysis

**Status: WIP / blob misidentified.** Blob 2122 in `Shaders011.fxp` is **not**
the deferred-composite pixel shader. Neither is blob 556. The actual
deferred-composite PS is not packed in `Shaders011.fxp` at all and is not
embedded in `Fallout4.exe` either; it is materialised at runtime by a path the
current static analysis does not cover.

This document captures the negative findings so the next campaign doesn't
repeat the same misclassification.

* Source ASMs:
  * `Fallout4RE/Scratch/shaders-extracted/ShadersFX/index/Shaders011/asm/Shaders011.2122.af996dd590c2.dxbc.asm` (newly materialised this campaign)
  * `Fallout4RE/Scratch/shaders-extracted/ShadersFX/index/Shaders011/asm/Shaders011.0556.3d7efefcfa9c.dxbc.asm` (pre-existing)
* Dispatch site C++: `DrawWorld::DeferredComposite`
  (REL::ID `{OG=728427, NG=2318313, AE=2318313}`,
   AE RVA `0x021F0790`, NG RVA `0x0209B100`, OG RVA `0x02855E60`)

## Three independent findings, all negative

### Finding 1: blob 2122 is not a fullscreen composite

The disassembly (105 instructions, sha1 `af996dd590c21197668ecfc81d72566dabffe3e4`)
has these binding and structural properties incompatible with the role:

* **Per-vertex inputs**: `v1.xyz` TEXCOORD0, `v3.xy` TEXCOORD1, `v4.xyz` TEXCOORD5.
  A fullscreen deferred-composite quad would have only `SV_POSITION` (`v0.xy`)
  and nothing else; the multiple per-vertex TEXCOORD/POSITION attributes mean
  this is drawn from real geometry (a sky dome, a sun disc, a screen-space
  quad with custom interpolators, or similar).
* **No albedo SRV**: bound SRVs are `t4`, `t9`, `t10` (all `texture2d`).
  Deferred composite must sample `kGbufferAlbedo` (R8G8B8A8_SRGB); this
  shader doesn't.
* **Does its own BRDF**: ASM L23-L34 compute sun-direction lighting against
  the decoded normal:
  * `cb0[2].xyz` = sun direction (dotted with view ray for spec, dotted with
    normal for diffuse).
  * `cb0[2].w` * `cb0[3].xyz` = sun colour.
  * `cb1[8].x` = specular power.
  * `cb1[0].w` = sky-cos lobe power.
  This is the wrong shape for "combine pre-computed diffuse + pre-computed
  specular + albedo into the main scene". The composite has already been
  done upstream of this draw.
* **Octahedral normal from t4 (R16G16_UNORM elsewhere) decoded via**
  `v3.xy * 2 - 1 ; sqrt(1 - dot2/4)` (ASM L0-L6). It pulls a per-vertex
  octahedral-encoded UV from `v3` and samples `t4`; not a screen-space read.
* **Fog block at L67-L102** with the same `cb12[41-46]` constants we
  documented in `shader-3560-analysis.md` for the ambient/IBL pass; consistent
  with a deferred lighting subpass that participates in the same scene-fog
  pipeline, but doesn't make this the composite.

The most plausible interpretation: blob 2122 is a **per-light deferred draw
for a lit geometry pass** (sun-disc, distant terrain, sky dome, etc.) that
samples the previously-written `kDiffuseBuffer` (`t9`) and `kSpecularBuffer`
(`t10`) at clamped screen UV and additively blends its own lit contribution
into them. Not the composite.

### Finding 2: blob 556 is a decal / particle shader

The disassembly (45 instructions, sha1 `3d7efefcfa9c7a61bb6f85239b90ff05bf251526`)
has:

* Per-vertex inputs `v1.xyz` (TEXCOORD0), `v2.xyzw` (COLOR1), `v3.z` (TEXCOORD5)
  — drawn from a particle/decal mesh, not a fullscreen quad.
* `t0` sampled at per-vertex UV `v1.xy` (decoded with `pow(x, 1/2.2)` gamma
  decode at L24-26).
* `t3` indexed via `ftoi(SV_Position.xy)` — depth read at the pixel's screen
  position.
* `t4` sampled at a per-vertex computed UV `r1.xy` (cube/sky lookup probably).
* `discard_nz` based on a distance-fade vs depth (L34-38) — typical decal
  edge-clip behaviour.
* Output: `o0.xyz = lerp(albedo, v2.rgb, v2.a)` (vertex-colour blend), `o0.w
  = pow(t0.r, 1/2.2)`.

This is a **particle/decal shader**, not a deferred composite. The Phase-A
classifier matched it on `3 SRVs + 1 RT` shape alone; the shape is right but
the inputs and the discard test rule it out.

### Finding 3: DrawWorld::DeferredComposite is a multi-dispatch orchestrator

Cross-runtime cache (`Fallout4RE/Workspace/.cache/cross-runtime.sqlite`,
`ghidra_function_calls`) shows AE `DrawWorld::DeferredComposite` at
`0x021F0790` issues **58 callees** including:

* **3× `BSBatchRenderer::RenderPassImmediately`** at offsets +0x01F5, +0x0915,
  +0x09DE (each preceded by `BSShaderManager::GetShader` + `BSRenderPass`).
* **4× `BSShaderAccumulator::RenderGeometryGroup`** at offsets +0x02BB,
  +0x04A6, +0x0A0C and another late.
* Setup/teardown calls to `RenderTargetManager::SetCurrentDepthStencilTarget`,
  `AcquireDepthStencil`/`ReleaseDepthStencil`, `AcquireRenderTarget`/
  `ReleaseRenderTarget`, `SetClearColor`/`RestorePreviousClearColor`.
* One `BSDFCompositeShaderEnvmapAccumulator::CopyPendingTexturesToArray` at
  +0x0024 — the envmap probe-array accumulator preamble.
* One `BSShaderManager::SetRenderMode` near the end.

So the C++ `DeferredComposite` function is not "the composite shader" — it
is the orchestrator that sets up four render-target switchovers, issues three
discrete pixel-shader passes, dispatches four geometry-group renders through
the batch renderer (each of which can invoke many per-geometry pixel shaders),
and tears the state back down. The expectation in
`exports/cs-lighting-shader-id-map.json` that "the deferred composite" is a
single PS reading `kGbufferAlbedo + kDiffuseBuffer + kSpecularBuffer` and
writing `kMain` is at best a simplification.

### Finding 4: the actual composite PS is not packed in Shaders011.fxp

Live RenderDoc capture `FO4_frame5407.rdc` (D3D11, 2.6 GB) — same capture
used for the ambient/IBL Phase C analysis — places the per-frame deferred
chain in this order:

| eid    | PS sha1[:12]   | size  | out RT format    | SRV count | role |
|--------|----------------|-------|------------------|-----------|------|
| 45345  | `761d41008016` | 9164  | R11G11B10_FLOAT  | 14        | Ambient / IBL (shader-3560-analysis.md) |
| **45368** | **`813c9acec23b`** | **3172** | **R11G11B10_FLOAT** | **6** | **Deferred composite (first follow-up PS, writes the same RT 172 = kMain that ambient/IBL just wrote)** |
| 45401-45623 | `46b911cb8053` × 22 | 2188  | R11G11B10_FLOAT  | 1 (D24S8) | Sun-shadow projection per cascade |
| 45650+ | various small  | ~1k   | R11G11B10_FLOAT  | 1         | Decals, particles |

The PS at **eid 45368** is the actual deferred-composite candidate. Its sha1
prefix is **`813c9acec23b`**. It writes the same `RT 172 = kMain` that
ambient/IBL writes, reads 6 SRVs (mix of R8G8B8A8_SRGB, R8G8B8A8_UNORM,
R11G11B10_FLOAT formats — exactly the deferred-composite ingredient list).
Bound CB count is 2.

But: scanning **all 3939 DXBC blobs** in `Shaders011.fxp` for sha1 prefix
`813c9acec23b` returns **zero matches**. The same prefix doesn't match any
of the **924 embedded DXBC blobs** in `Fallout4.unpacked.exe` either. So the
actual deferred-composite pixel shader is loaded from a source the current
toolchain hasn't yet identified.

Possibilities (none verified this campaign):

1. **Compiled at runtime from raw bytecode in a `.dat` shaderpack** that
   gets streamed in early but isn't archived under `Fallout4 - Shaders.ba2`.
2. **Loaded from D3D11 driver internals** (e.g., RenderDoc's bytecode is
   slightly transformed during capture and the SHA differs from the
   on-disk SHA — possible but unusual).
3. **Generated by RenderDoc's replay layer itself** for the captured eid
   (e.g., from a `CreatePixelShader` call whose source bytes came from a
   `ResourceMemoryRecord` we haven't extracted).

Resolving this requires either:

* a RenderDoc `Save Shader Bytecode` of eid 45368's PS and a byte-by-byte
  hunt for that exact blob across every loose file in the FO4 install
  directory, OR
* dynamic instrumentation of `ID3D11Device::CreatePixelShader` during the
  deferred-composite frame to record the bytecode source pointer and
  back-trace it to the file it came from.

Both are outside the budget of this prompt.

## Honest recommendation

The deferred-composite reconstruction **cannot be completed against blob 2122
or blob 556** because neither is the right shader. The destination
`shaders/lighting/deferred_composite.hlsl` should stay at `#error` until
either:

1. The actual PS bytecode for eid 45368 is extracted from RenderDoc and
   located in some FO4 asset file (then ran through `disassemble-dxbc.py`
   + the runbook's manual reconstruction stage), OR
2. The DXBC loader path that materialises the bytecode is identified, and
   the corresponding file added to the indexer.

Pending that, this campaign's deliverable is the negative findings above
plus the followups entries in
`shaders/lighting/docs/lighting-shader-followups.md` under
`### Shaders011.2122`.

## Cross-references

* `shader-3560-analysis.md` — ambient/IBL pass that fires immediately
  before this missing composite at RenderDoc eid 45345.
* `lighting-shader-id-map.json` — REL::ID + RVA triplet for the
  `DrawWorld::DeferredComposite` C++ host site (still correct; the host
  function is real, only the PS-shader identification was wrong).
* `Fallout4RE/Scratch/reports/rdoc-deferred-composite-walk.json` — the
  live-capture walk that surfaced eid 45368 as the actual composite PS.
* `Fallout4RE/Scratch/reports/rdoc-ps-output-survey.json` — per-RT draw
  histogram showing RT 172 (= kMain) receives 253 draws across 42 unique
  PS in this frame (the deferred-composite chain plus all
  alpha-blended-into-scene draws).
