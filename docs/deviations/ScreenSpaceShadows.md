# Screen Space Shadows

Rules, Kind legend and cross-cutting records: [README](README.md).

Feature classification: **core** (rule 2: upstream screen-space-shadow algorithm and design).
Distant-tree/alpha coverage is Pending porting work.

Upstream pin: `d330bf12d`. Code: `features\ScreenSpaceShadows`, consumers in
`package\Shaders\DFLight.hlsl` through `FO4/ScreenSpaceShadowConsumer.hlsli`.
All three upstream shaders and Bend's CPU header are consumed unchanged through `xmake\shared.lua`.
BendSettings names, keys, defaults and edit ranges match upstream; its own b1 dispatch constants
carry the settings. The output is upstream's R8G8_UNORM front/back visibility, cleared to white
each frame, with no history. Canonical t17 remains shared and includes reprojected near depth;
Bend reads it directly at CS t0, so first person casts as upstream does.

## Translations

| Kind | Upstream | Fallout 4 | Why / evidence | Where |
|---|---|---|---|---|
| Forced | Ordinary world-projection scene depth and its matching VP | Bind canonical R32_FLOAT (`GetCanonicalSceneDepthSRV()`, t17 content) directly to Bend's unchanged CS t0, using the copied world+jitter record's row-vector ViewProjection; reprojected first-person depth remains eligible to cast | fallout4-re engine-facts Native composite depth partition, b12 near reprojection and World record at PostDeferredPrePass: native world depth is `mad(raw,1.01,-0.01)` and near depth has a different projection. The R32 declaration is selected by upstream's `TERRAIN_BLENDING` define; no terrain feature is implied | `CanonicalDepth.cpp`, `FO4/CanonicalDepthCS.hlsl`, `ScreenSpaceShadows.cpp` `GetComputeRaymarch` / `OnPreDeferredLights` |
| Forced | First-person forward materials never receive SSS | Explicit receiver gate returns 1 for raw depth `<=0.01` in FO4's shared deferred light passes; first person casts as upstream does through canonical depth, with no caster exclusion | Upstream `Lighting.hlsl:2224–2231` guards the consumer with `SCREEN_SPACE_SHADOWS && DEFERRED`; `deferredPass` is cleared by `EndDeferred` (`Deferred.cpp:449`, called inside RenderWorld at `693–695`) before RenderFirstPersonView. Upstream's Z-prepass depth copy contains first-person depth and Bend has no caster exclusion (see `sss-first-person-caster.md` research evidence). FO4 lights world and first person in shared deferred passes (fallout4-re engine-facts Native composite depth partition, BSLighting forward-pass source), requiring this receiver gate | `FO4/ScreenSpaceShadowConsumer.hlsli`, WARP receiver-gate fixture in `ShaderCompileTests.cpp` |
| Fix | Consumer adds 0.5 to pixel-centered SV_POSITION before integer conversion | Shared-fork seam `6f81ebc25` samples the pixel directly; no FO4 counter-offset | Upstream Lighting, DistantTree and RunGrass pass pixel-centered SV_POSITION just like FO4 DFLight; Bend writes floored pixel coordinates. Upstream PR candidate listed above | Shared `ScreenSpaceShadows/ScreenSpaceShadows.hlsli` |
| Forced | Normalize and negate the active sun light's propagation direction | Normalize FO4 sun world-rotation row zero and project its negative with w=0 | fallout4-re engine-facts Sun light orientation / Deferred sun constant: row zero is sun-to-scene; native BSDFLight negates that same worldDirection into view-space toward-light b2 c1 | `World/Sky.cpp` `TryGetSunDirectionWS`, `ScreenSpaceShadows.cpp` `OnPreDeferredLights` |
| Forced | Prepass before material lighting consumers | Clear/dispatch before DeferredLightsImpl, sample unchanged upstream t45 through an FO4 include in directional light and focused shadow families, release the owned binding afterward | fallout4-re engine-facts Sun light passes and BSLighting forward-pass source: FO4 ordinary world/first-person materials are prepass-drawn and lit in deferred light passes. Reconstructed BSDFLight owns native t0–t5, not t45; no consumer-slot translation is needed | `ScreenSpaceShadows.cpp` `Load` / `BindShadowMask` / `OnPostDeferredLights`, `DFLight.hlsl` directional and shadow-only families |
| Forced | Lighting.hlsl separates direct visibility x and transmission visibility x/y by facing | Directional split families apply x to their native shared shadow term when front-facing, y to back-facing wrap/transmission; the no-cascade family applies direct and transmission separately | Reconstructed `DFLight.hlsl` DIRSPLITS1/2/3 combines direct and transmission into finalDiffuse before multiplying the shared shadow, while UNSHADOWED has no shared shadow. Front transmission already receives x through that term, so the extra multiplier is back-facing only; no new shading model is introduced | `FO4/ScreenSpaceShadowConsumer.hlsli` `FO4BackTransmissionScreenSpaceShadow`, `DFLight.hlsl` `backfaceWrap` / `forwardBlend` and UNSHADOWED directional block |
| Framework | Skyrim feature lifecycle, JSON persistence and shader activation | FO4 load=false activation, upstream-cased TOML keys, forwarding-only UI, ownership/hash gate, full-extent white R8G8 fallback with allocation backoff, telemetry and mask preview | Repository lifecycle, persistence and fail-closed renderer policies are host architecture, not engine-imposed algorithm differences. `Enable` toggles generation live; disabled/non-full-sky frames stay white without a shared feature block | `ScreenSpaceShadowsSettings.h`, `ScreenSpaceShadows.cpp`, `ScreenSpaceShadowsResources.h` |

## Pending

| Kind | Upstream | Notes / where |
|---|---|---|
| Pending | DistantTree's 0.8 SSS strength and forward/alpha receiver coverage | Deferred material receivers have no identified distant-tree discriminator; do not invent one or bind the mask to unreached forward routes. fallout4-re engine-facts Stock forward-pass census leaves distant-tree relighting and alpha route coverage for capture proof; `DFLight.hlsl`, `DistantTree.hlsl`, native Lighting family |

