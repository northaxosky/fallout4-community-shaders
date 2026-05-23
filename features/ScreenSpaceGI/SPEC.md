# ScreenSpaceGI feature

XeGTAO + Visibility Bitmask + SH2-YCoCg compute chain ported from upstream Skyrim Community Shaders @ `bb6460db`. Produces ambient occlusion (R8) and SH2-YCoCg indirect-light buffers from the deferred-renderer depth + normal + lit-colour + motion-vector RTs.

The Phase 1 AO-only path was removed in this revision; v2 is now the only SSGI implementation. The transitional `Apply()` pass keeps blending the new AO output into `kDiffuseBuffer` until the Phase 2c.2 ambient-injection rewrite lands.

## Hook surface

- `RegisterPostDeferredPrePass()` runs `DrawSSGI()` (7-shader compute fan-out).
- `RegisterPostDeferredLightsImpl()` runs `Apply()` while the transitional apply pass is the SSGI consumer.

## Runtime assets

- Pipeline shaders live in `features/ScreenSpaceGI/Shaders/SSGIv2/` and deploy to `Data\F4SE\Plugins\ScreenSpaceGI\SSGIv2\`.
- The transitional `ApplyAOCS.hlsl` lives in `features/ScreenSpaceGI/Shaders/` and deploys to `Data\F4SE\Plugins\ScreenSpaceGI\`.
- EA FastNoise sampler (`fast_2uges.dds`) ships under `SSGIv2/`; license header in `LICENSE-EA-FastNoise.md`.
- Settings live in `Data\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceGI.toml`.

## Invariants

- 7-shader fan-out order: `prefilterDepths -> prefilterNormal -> radianceDisocc -> prefilterRadiance -> gi -> blur -> upsample`. The dispatch bails before binding anything if any compute slot failed to compile.
- Working-resolution textures (depth/normal/radiance pyramids + AO/IL buffers) reallocate on (frameW, frameH, resolutionMode) change.
- Projection reconstruction reads `BSGraphics::State::cameraDataCache` (preferred jittered entry that matches the DrawWorld current-camera global) and falls back to `state->cameraState` then the historical FOV.
- Constant buffers must stay 16-byte aligned (`SSGICB` = 288 bytes, `ApplyCB` = 16 bytes; `static_assert` enforced).
- The transitional `Apply()` reads `texAo[inputAoIdx]` (post-frame-swap) and blends into `kDiffuseBuffer`. Known regression while Phase 2c.2 is unshipped: vanilla SSAO is still active in parallel, so default `applyIntensity` is kept low (0.5) to avoid 2x darkening.

## Smoke gates

- `scripts/smoke-ssgi-sweep.sh`
- `scripts/smoke-ssgi-substrate-warmup.sh` (validates the full 7-shader chain compiles + first-fire log line)

Smoke markers include `.ssgi_force_apply` and `.ssgi_extreme`, read during `LoadSettings()`.
