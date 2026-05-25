# Divergence from upstream

Fallout 4 Community Shaders tracks Skyrim Community Shaders where the renderer, runtime layout, and user workflow line up. The items below are the deliberate places where this port should not be pulled back toward Skyrim CS without a matching FO4 reason. All upstream citations refer to `community-shaders/skyrim-community-shaders` at SHA `bb6460d`.

## Cross-feature TOML presets

### What upstream Skyrim CS does

Skyrim CS stores settings in aggregate JSON files and layers default settings, user settings, mod-authored overrides, and user override files during `State::Load` (`community-shaders/skyrim-community-shaders:src/State.cpp:275-390`). Its closest related system is `SettingsOverrideManager`, which discovers `{ModName}_{FeatureShortName}.json` and `{ModName}_Global.json` under `Overrides` and recursively merges JSON overrides for mods (`community-shaders/skyrim-community-shaders:src/SettingsOverrideManager.h:12-21`). It does not ship a user preset library or a `Presets` directory.

### What this port does

We have `cs::PresetManager`, a cross-feature TOML preset library under `Data\F4SE\Plugins\FO4CommunityShaders\Presets\`. A preset can capture Imagespace, Screen Space Shadows, and Screen Space GI in one file. Applying a preset stages every participating feature, swaps live state only after parsing succeeds, then lets each feature persist and refresh derived resources.

### Why FO4 or the architecture diverges

FO4 users often build a complete look from tonemapping, LUTs, bloom, contact shadows, and ambient bounce together. A single look preset is more useful than per-feature snippets because changing one part can make the others look wrong. The staged apply path also fits this port's resource model: a LUT cache reload, shader permutation invalidation, or per-weather profile rebuild should not happen halfway through another feature's parse.

### Forward path

Convergence should mean adding a separate override layer for mod authors, not replacing `PresetManager`. A future `OverrideManager` can borrow upstream's layering model while keeping TOML and the existing staged apply contract.

## Typed callback broker for render hooks

### What upstream Skyrim CS does

Skyrim CS installs central hooks and dispatches feature virtuals through `Feature::ForEachLoadedFeature` (`community-shaders/skyrim-community-shaders:src/Feature.h:239-260`). Deferred feature work is called from shared prepass functions such as `Deferred::PrepassPasses` (`community-shaders/skyrim-community-shaders:src/Deferred.cpp:206-220`). Hook installation is owned by central hook code, with each anchor having one owner (`community-shaders/skyrim-community-shaders:src/Hooks.cpp:825-858`).

### What this port does

`cs::engine::RenderHooks` is a typed callback broker. It owns one detour per FO4 deferred anchor, keeps subscriber lists, and orders callbacks with `HookPriority`. Deferred prepass, deferred lights, deferred composite, and the post-dynamic-resolution viewport call site all go through the broker.

### Why FO4 or the architecture diverges

FO4 exposes useful insertion points as separate engine functions, and multiple features need the same ones. Letting every feature detour `DeferredLightsImpl` would create a fragile chain where install order is the hidden contract. The broker makes ownership explicit: one patch at the engine boundary, many plain callbacks inside the plugin.

### Forward path

Keep the broker as the port's render extension surface. If upstream ever grows a comparable multi-subscriber hook layer, align names and ordering semantics. The post-dynamic-resolution viewport path can also be generalized onto the same priority list if a third feature needs that slot.

## ComputeScope around compute dispatches

### What upstream Skyrim CS does

Skyrim CS cleans up compute state inline in each feature. Screen Space Shadows clears the SRV, UAV, shader, sampler, and constant-buffer slots it bound (`community-shaders/skyrim-community-shaders:src/Features/ScreenSpaceShadows.cpp:267-279`). It does not use a shared RAII compute guard. Upstream's shared buffer helper focuses on buffer descriptors and alignment helpers (`community-shaders/skyrim-community-shaders:src/Buffer.h:31-60`).

### What this port does

`cs::ComputeScope` saves the current OM render targets and depth-stencil view, unbinds OM while compute runs, clears the full D3D11 CS slot ranges on exit, restores OM, and releases the references acquired by `OMGetRenderTargets`.

### Why FO4 or the architecture diverges

FO4's deferred renderer leaves render targets such as `kDiffuseBuffer` and the depth target bound around places where this port injects compute work. A feature that binds the same texture as an SRV or UAV without first unbinding OM risks D3D11 hazards and dirty state leaking into the next engine draw. A shared guard is safer than relying on each feature to remember the exact cleanup shape.

### Forward path

Keep using `ComputeScope` for injected compute passes. If a narrower state guard becomes useful, build it on top of the same save, unbind, clear, and restore contract rather than returning to feature-local cleanup.

## Per-feature TOML files

### What upstream Skyrim CS does

Skyrim CS uses `Data\SKSE\Plugins\CommunityShaders\SettingsDefault.json`, `SettingsUser.json`, `SettingsTest.json`, and `SettingsTheme.json` (`community-shaders/skyrim-community-shaders:src/Utils/FileSystem.cpp:60-78`). Features load and save their slice of one aggregate JSON tree through `Feature::LoadSettings(json&)` and `Feature::SaveSettings(json&)` (`community-shaders/skyrim-community-shaders:src/Feature.h:114-119`).

### What this port does

Each feature owns its own TOML file under `Data\F4SE\Plugins\FO4CommunityShaders\`, for example `Imagespace.toml`, `ScreenSpaceGI.toml`, `ScreenSpaceShadows.toml`, `FrameGeneration.toml`, and `RenderDoc.toml`. The base feature lifecycle has no JSON argument; features read, validate, and write their own TOML.

### Why FO4 or the architecture diverges

TOML is easier to hand-edit and can carry comments next to path-heavy settings such as DLL paths, LUT paths, and capture folders. Per-feature files also match this port's packaging model, where features can be installed, smoke-tested, and reset independently. One broken feature config should not require regenerating an entire aggregate settings file.

### Forward path

Do not migrate to upstream's aggregate JSON format just for symmetry. If contributors need a whole-plugin snapshot, add explicit import or export tooling that writes the existing per-feature TOMLs and preset files.

## snake_case TOML keys

### What upstream Skyrim CS does

Skyrim CS uses `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT`, so C++ member names become JSON keys directly. Screen Space Shadows writes PascalCase keys such as `Enable`, `SampleCount`, `SurfaceThickness`, `BilinearThreshold`, and `ShadowContrast` (`community-shaders/skyrim-community-shaders:src/Features/ScreenSpaceShadows.cpp:14-20`).

### What this port does

TOML keys are lowercase `snake_case` under `[settings]`, such as `sample_count`, `surface_thickness`, `tonemap_operator`, `frame_gen_type`, and `lut_path`.

### Why FO4 or the architecture diverges

The TOML schema is a user-facing file format, not a dump of C++ member names. `snake_case` is easier to scan in TOML, stays stable across C++ refactors, and is consistent across the port. It also avoids mixing JSON-style PascalCase with TOML tables that already use lowercase names.

### Forward path

Keep `snake_case` as the canonical schema. If we add upstream-setting import helpers, make them translate PascalCase JSON names to TOML keys rather than teaching the live schema two naming styles.

## FO4-original feature set

### What upstream Skyrim CS does

The upstream feature list includes rendering features such as True PBR, Screen Space Shadows, Screen Space GI, Performance Overlay, RenderDoc, Upscaling, Screenshot, Linear Lighting, HDR Display, and many others (`community-shaders/skyrim-community-shaders:src/Feature.cpp:211-249`). It does not include MotionVectorFixes, FrameGeneration, ShaderCatalog, or ShaderReplacement. Its shader infrastructure compiles source HLSL into cache entries through `ShaderCache` (`community-shaders/skyrim-community-shaders:src/ShaderCache.cpp:1344-1496`).

### What this port does

MotionVectorFixes repairs FO4 motion-vector inputs for temporal effects. FrameGeneration bridges the D3D11 game to D3D12 frame-generation SDKs for FSR3, DLSS-G, and XeSS-FG. ShaderCatalog records FO4-created shader hashes, metadata, and attribution into SQLite. ShaderReplacement swaps selected reconstructed FO4 pixel shaders at creation time for validation and development.

### Why FO4 or the architecture diverges

These features answer FO4 problems. The Next-Gen Update's temporal path needs `previousWorld` repairs. Frame-generation SDKs require D3D12 interop even though FO4 renders through D3D11. FO4 also ships important deferred-pipeline shaders as compiled blobs, so cataloging and replacement are useful development tools. Skyrim CS owns a source-shader cache instead, so the same tools would not solve the same problem there.

### Forward path

Treat these as native FO4 features. Borrow upstream UI, feature lifecycle, cache hygiene, and testing ideas where they fit, but do not try to map these features onto upstream modules one-for-one.

## Imagespace as a native post-process stack

### What upstream Skyrim CS does

Skyrim CS has no Imagespace feature in its static feature list (`community-shaders/skyrim-community-shaders:src/Feature.cpp:211-249`). Its post-lighting compute work lives in features with specific renderer goals, such as Screen Space GI inside `DeferredCompositeCS.hlsl` (`community-shaders/skyrim-community-shaders:package/Shaders/DeferredCompositeCS.hlsl:47-84`).

### What this port does

Imagespace owns tonemapping, LUT application, adaptive exposure, bloom, vignette, chromatic aberration, sharpening, lens flare, lens dirt, and depth of field inside the plugin. It runs after upscaling and before frame-generation capture so the captured HUDless frame contains the final post-process result.

### Why FO4 or the architecture diverges

FO4 users often rely on ENB or ReShade for this part of the look, but those tools do not compose cleanly with this port's upscaling and frame-generation path. A native stack can see the engine buffers, respect feature ordering, use the same menu and TOML model, and feed frame generation with the right image.

### Forward path

Converge with community expectations rather than upstream feature shape. Useful additions include common ReShade tonemap operators, better LUT conversion guidance, and optional user-supplied lens assets. The feature should remain native to this port.

## Three-runtime REL::ID tuples

### What upstream Skyrim CS does

Skyrim CS commonly addresses SE and AE with `REL::RelocationID(SE, AE)`, while VR-specific hooks use separate variant helpers where needed. The central hook installer shows both the common two-runtime pattern and the exceptional variant forms (`community-shaders/skyrim-community-shaders:src/Hooks.cpp:829-858`).

### What this port does

FO4 hooks use `REL::ID({ OG, NG, AE })`. NG and AE often share an Address Library ID, but the tuple still records all three supported runtimes at the call site.

### Why FO4 or the architecture diverges

Fallout 4 has three supported runtime lines for this project: original, Next-Gen, and the current AE line. Hook safety depends on resolving the right address for each executable. Collapsing the notation to look like Skyrim's two-runtime form would hide a real compatibility axis.

### Forward path

Keep the three-value form until CommonLibF4 offers a clearer abstraction with the same information. For new hook anchors, cite the reverse-engineering export or other source next to the ID so the tuple can be re-derived when a runtime changes.

## ScreenSpaceShadows composite blend

### What upstream Skyrim CS does

Skyrim CS binds the R8 screen-space shadow texture as a pixel-shader SRV and reads it with `ScreenSpaceShadowsTexture.Load(...)` (`community-shaders/skyrim-community-shaders:features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/ScreenSpaceShadows.hlsli:4-8`). In deferred lighting, it multiplies that mask into `dirDetailedShadow` after the engine shadow term when `SCREEN_SPACE_SHADOWS`, `DEFERRED`, `!SharedData::InInterior`, and `dirLightAngle >= 0.0` are true (`community-shaders/skyrim-community-shaders:package/Shaders/Lighting.hlsl:2509-2524`). That keeps the blend in linear direct-lighting math before tonemapping, and the resulting directional light context applies it to diffuse and specular lighting.

### What this port does

The FO4 port runs a separate compute apply pass after `DeferredLightsImpl`. That pass point-loads the same R8 mask, reconstructs `N.L` from `kGbufferNormal`, samples `kDiffuseBuffer`, writes attenuated diffuse lighting to a scratch UAV, then copies the result back to `kDiffuseBuffer`. With `sun_only=true`, the attenuation is `lerp(1.0, mask, saturate(smoothstep(0.05, 0.30, N.L) * ApplyContrast))`; with `sun_only=false`, the smooth gate is bypassed and `ApplyContrast` controls a global mask multiply.

### Why FO4 or the architecture diverges

FO4's deferred lighting and composite shaders are engine-owned code, not source files this project compiles at startup. The post-lighting buffer does not expose Skyrim's `dirDetailedShadow` scalar or a separated sun contribution, so `kDiffuseBuffer` is the clean extension point available today. The `sun_only` gate is intentionally conservative because that buffer can also contain non-sun diffuse light; the current pass does not attenuate `kSpecularBuffer`.

### Forward path

If this port later owns a complete deferred-lighting replacement, move the modulation into that shader and compare against upstream's in-shader math. That path should use an upstream-style `dirDetailedShadow *= mask` blend and add matching specular attenuation while keeping the mask read point-filtered.

## Choosing write_thunk_call or detour_thunk

### What upstream Skyrim CS does

Skyrim CS uses both mechanisms. It detours function entries when every caller should be intercepted, for example `BSShader::LoadShaders`, and patches individual call sites when only one call should be redirected, for example input polling or render-target creation calls (`community-shaders/skyrim-community-shaders:src/Hooks.cpp:832-858`).

### What this port does

The same rule applies here. `detour_thunk` is used for FO4 deferred-renderer anchors such as `DeferredPrePass`, `DeferredLightsImpl`, and `DeferredComposite`. `write_thunk_call` is used for the post-dynamic-resolution viewport re-arm because only the single call site in the composite epilogue should be intercepted.

### Why FO4 or the architecture diverges

The mechanism is part of the anchor contract. A function-entry detour catches all callers and is appropriate for named engine stages. A call-site patch is narrower and safer when only one call has the desired ordering semantics. Using `detour_thunk` for the viewport helper would catch unrelated callers; using `write_thunk_call` for deferred lights would miss any path that reaches the function through another call site.

### Forward path

Keep documenting the intended scope when adding hooks. Use `detour_thunk` for whole-function renderer stages, and use `write_thunk_call` only when a specific call site is the feature boundary. Call-site hooks should keep opcode or offset validation where practical.
