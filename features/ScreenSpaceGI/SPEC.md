# ScreenSpaceGI feature

XeGTAO + Visibility Bitmask + SH2-YCoCg compute chain ported from upstream Skyrim Community Shaders @ `bb6460db`. Produces ambient occlusion (R8) and SH2-YCoCg indirect-light buffers from the deferred-renderer depth + normal + lit-colour + motion-vector RTs.

The Phase 1 AO-only path was removed in this revision; v2 is now the only SSGI implementation. Two post-deferred-lights consumers fold the chain outputs into `kDiffuseBuffer`: `Apply()` modulates it with the AO buffer, then `ApplyIL()` adds the SH-reconstructed indirect bounce on top. The dual-compute Phase 2c.2 split keeps the bounce term outside the AO darkening window, matching the upstream Skyrim CS composite semantics (`linDiffuseColor += ssgiIl * linAlbedo`) without requiring a `deferred_composite.hlsl` fork.

## Hook surface

- `RegisterPostDeferredPrePass()` runs `DrawSSGI()` (7-shader compute fan-out).
- `RegisterPostDeferredLightsImpl()` runs `Apply()` (AO darken) and then `ApplyIL()` (IL bounce). Registration order = fire order; bounce must not be modulated by AO from its own surface.

## Runtime assets

- Pipeline shaders live in `features/ScreenSpaceGI/Shaders/SSGIv2/` and deploy to `Data\F4SE\Plugins\ScreenSpaceGI\SSGIv2\`.
- Consumer shaders `ApplyAOCS.hlsl` + `ApplyILCS.hlsl` live in `features/ScreenSpaceGI/Shaders/` and deploy to `Data\F4SE\Plugins\ScreenSpaceGI\`.
- EA FastNoise sampler (`fast_2uges.dds`) ships under `SSGIv2/`; license header in `LICENSE-EA-FastNoise.md`.
- Settings live in `Data\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceGI.toml`.

## Invariants

- 7-shader fan-out order: `prefilterDepths -> prefilterNormal -> radianceDisocc -> prefilterRadiance -> gi -> blur -> upsample`. The dispatch bails before binding anything if any compute slot failed to compile.
- Working-resolution textures (depth/normal/radiance pyramids + AO/IL buffers) reallocate on (frameW, frameH, resolutionMode) change.
- Projection reconstruction reads `BSGraphics::State::cameraDataCache` (preferred jittered entry that matches the DrawWorld current-camera global) and falls back to `state->cameraState` then the historical FOV.
- Constant buffers must stay 16-byte aligned (`SSGICB` = 288 bytes, `ApplyCB` = 16 bytes, `ApplyILCB` = 16 bytes; `static_assert` enforced).
- `Apply()` and `ApplyIL()` share `scratchDiffuse` (single full-res R11G11B10F UAV target). `Apply()` writes (existing direct light * AO) and copies back to `kDiffuseBuffer`. `ApplyIL()` reads the (now AO-modulated) `kDiffuseBuffer`, adds the SH-reconstructed bounce, and copies back. Composite later multiplies the result by albedo, so the bounce term must NOT be pre-multiplied by albedo in `ApplyILCS.hlsl`.
- The Apply chain reads `texAo[inputAoIdx]` / `texIlY[inputIlIdx]` / `texIlCoCg[inputIlIdx]` post-frame-swap (DrawSSGI swaps input/output at end of frame).
- Coordinate frame: with `CameraViewInverse` currently fed as identity (per-frame matrix capture lands in 2c.3), gi.cs's "world-space" SH evaluation is effectively view-space. `ApplyILCS.hlsl` matches that frame by hemisphere-reconstructing the view-space normal from `kGbufferNormal` directly, mirroring the `EncodeFO4Normal` input pattern in `prefilterNormal.cs.hlsl`. Once 2c.3 wires per-frame matrices, both producer and consumer move to true world-space.

## Smoke gates

- `scripts/smoke-ssgi-sweep.sh`
- `scripts/smoke-ssgi-substrate-warmup.sh` (validates the full 7-shader chain compiles + first-fire log line)

Smoke markers include `.ssgi_force_apply` and `.ssgi_extreme`, read during `LoadSettings()`.
