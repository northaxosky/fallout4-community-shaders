# `shaders/lighting/` follow-ups

Persistent tracker for `// TODO: identify` markers and unresolved questions
surfaced by lighting-shader-reconstruction campaigns. New entries append
under their shader-blob header. Migrate, do not delete.

## Shaders011.2122 / `deferred_composite.hlsl`

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
