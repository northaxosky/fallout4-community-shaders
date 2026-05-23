# ScreenSpaceGI feature

XeGTAO + Visibility Bitmask + SH2-YCoCg compute chain ported from upstream Skyrim Community Shaders @ `bb6460db`. Produces ambient occlusion (R8) and SH2-YCoCg indirect-light buffers from the deferred-renderer depth + normal + lit-colour + motion-vector RTs.

The Phase 1 AO-only path was removed in this revision; v2 is now the only SSGI implementation. Two post-deferred-lights consumers fold the chain outputs into `kDiffuseBuffer`: `Apply()` modulates it with the AO buffer, then `ApplyIL()` adds the SH-reconstructed indirect bounce on top. The dual-compute Phase 2c.2 split keeps the bounce term outside the AO darkening window, matching the upstream Skyrim CS composite semantics (`linDiffuseColor += ssgiIl * linAlbedo`) without requiring a `deferred_composite.hlsl` fork.

## Hook surface

- `RegisterPostDeferredPrePass()` runs `DrawSSGI()` (7-shader compute fan-out).
- `RegisterPreDeferredLightsImpl()` runs `ClearVanillaSAOTargets()` when the vanilla SAO disable lever is active: clears `kSSAO`/`kSSAOFinal`/`kSSAOFinalSwap`/`kSSAOFinalSwap2` to white so the deferred ambient/IBL pass sees full-unoccluded SAO state before we contribute the real AO from `Apply()`.
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
- The Apply chain reads AO + IL at "fresh" indices captured before the end-of-frame swap: `freshAoIdx` = gi's outIdx; `freshIlIdx` = blur's inIdx when `enableBlur`, gi's outIdx otherwise (blur writes IL to inIdx only because `TEMPORAL_DENOISER` is not defined). Without this, post-swap consumer reads would see gi's un-blurred IL.
- HALF_RES / QUARTER_RES architecture (Phase 2c.3): all pyramids + flat textures are allocated at full-res W*H regardless of mode; only the top-left work-res tile is populated. Sub-res shader variants (`HALF_RES` / `QUARTER_RES` defines) bound dispatches against `OUT_FRAME_DIM = FrameDim * (1/divisor)` and remap sample UVs via `OUT_FRAME_SCALE`. CB `FrameDim` is always full-res; the macros do the work-res derivation. Per-mode CS slots are compiled lazily by `GetCSVariant()`; `shadersWarmedForMode[3]` skips recompile for already-warmed modes. In HALF/QUARTER, four additional full-res scratch RTs (`texAoUpsampled` R8, `texIlYUpsampled` RGBA16F, `texIlCoCgUpsampled` RG16F, `texGiSpecularUpsampled` RGBA16F) hold the bilateral-upsample output and feed `Apply` / `ApplyIL`; in FULL the upsample stage is skipped entirely and the consumers sample `texAo[freshAoIdx]` / `texIl*[freshIlIdx]` directly. Memory cost of the upsample destinations: ~168 MB at 4K; 0 MB in FULL.
- Per-frame view-matrix capture (Phase 2c.3): `GetProjectionData()` extracts `camViewData.viewMat` from the same selected `cameraDataCache` entry it uses for the frustum. The raw matrix is stored as the transpose of the DXMath row-major view matrix, so `CameraViewInverse` and `PrevInvViewMat` upload as `XMMatrixInverse(XMLoad(raw))` with no follow-up transpose (the two transposes cancel algebraically). `PrevInvViewMat` only advances when current-frame capture succeeds; on fallback frames both slots get identity and history holds.
- Vanilla SAO disable lever (Phase 2c.3): at `OnDataLoaded()`, when `settings.enableVanillaSSAO == false` (default), patch `DrawWorld::ImagespaceSAO` (REL::ID 39691 OG / 2318306 NG+AE) first byte `0x48 -> 0xC3` so the function early-returns before its stack adjust, leaving SAO RTs unwritten by the engine. `ClearVanillaSAOTargets()` then writes white to all four SAO RTs each frame before the deferred ambient pass binds them as SRVs. The setting is restart-required by design; toggling it in the menu only takes effect on next launch.
- Quality presets (Phase 2c.3): `kQualityPresets[]` maps `Performance/Quality/Cinematic` to upstream Skyrim CS @ `bb6460db` `Low/Standard/Extreme` (Performance = quarter-res / 10 slices / 12 steps; Quality = half-res / 4 slices / 8 steps; Cinematic = full-res / 4 slices / 8 steps). The 5 builtin look-presets (`Default`, `Neutral-Realistic`, `Vivid-Daylight`, `Cinematic-Night`, `Reactor-Inspired`) pin `slice_count` / `step_count` / `resolution_mode` to match their declared `preset` tier and keep `apply_intensity` / `gi_strength` / `ao_radius` / etc. as artist look knobs.
- Coordinate frame: with per-frame matrix capture wired in 2c.3, gi.cs's SH evaluation runs in true world-space (camera-relative). `ApplyILCS.hlsl` currently still hemisphere-reconstructs the view-space normal from `kGbufferNormal` directly, so a world/view mismatch is possible at the consumer site; queued for verification.

## Smoke gates

- `scripts/smoke-ssgi-sweep.sh`
- `scripts/smoke-ssgi-substrate-warmup.sh` (validates the full 7-shader chain compiles + first-fire log line)

Smoke markers include `.ssgi_force_apply`, `.ssgi_extreme`, and `.ssgi_resmode` (read during `LoadSettings()`). `.ssgi_resmode` accepts `'0'` / `'1'` / `'2'` and overrides `settings.resolutionMode` after the Quality preset gets applied, so the smoke harness can validate FULL / HALF / QUARTER dispatch paths independently of preset tier.
