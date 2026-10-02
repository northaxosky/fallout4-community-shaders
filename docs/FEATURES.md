# Features

Classification of every upstream Skyrim Community Shaders feature (pin `d330bf12d`, shared fork `6f81ebc2`)
for Fallout 4, with port order. Per-feature deviation records live in [deviations](deviations/README.md).

## Classification

A feature is split into **slices**: separable passes, effects or shader paths with a named upstream boundary.
Each slice is classified; a feature is **core**, **extension**, **mixed** (core slices plus an extension
remainder) or **N/A**.

1. Non-visual tooling and diagnostics are always core.
2. A visual slice is core when it keeps upstream's algorithm and design. Translation never makes it an
   extension: hook anchors, logical render-target/slot mapping, cbuffer layouts, consumers in reconstructed
   stock shaders, data FO4 already produces read through a typed accessor, calibrated defaults, toggles,
   diagnostics, and relocating an upstream datum (same meaning) into a different FO4 container.
3. A visual slice is an extension when FO4 needs machinery upstream does not have: a new datum or transport
   across passes (typically because upstream shades forward and FO4 is deferred), an authoring/asset/metadata
   pipeline, a replaced engine subsystem, or an FO4-original algorithm.
4. Upscaling, FrameGeneration and MotionVectorFixes are core (doodlum FO4 release lineage).

Stock-shader reconstruction and missing reverse engineering are port work, not extension reasons.
`*` marks a core slice that is conditional on a named RE question. N/A means the problem is absent or FO4
already does it natively.

Calibration: Dynamic Cubemaps' dry-material reflections reuse FO4's native envmap selection (composite
`ENVMAP`, G-buffer probe slice/strength, native gain) and swap only the sampled cube: a small adapter, so core.
Upstream's 1×1 sentinel cubes cannot register in FO4 (t8 rejects cubes under 128 px), so authored
reflectance selection is an extension.

## Map

Status: **P** ported, **Pt** partial, **NS** not started. Payoff and effort cover all core slices.

| Feature | Verdict | Core slices | Extension / N/A slices | Status | Payoff | Effort | Conf. |
|---|---|---|---|---|---|---|---|
| CS Editor | Core | Weather/template/imagespace editor, godray record editor, light editor, shell | N/A: Skyrim VL noise fields | NS | H | L | High |
| Cloud Shadows | Core | Layer-cube producer, directional receivers, Effect/Water/Particle receivers, EHF attenuation | N/A: legacy VL consumer | NS | H | L | Med |
| [Dynamic Cubemaps](deviations/DynamicCubemaps.md) | Mixed | Capture/filter/inference, water, wet-film consumer, dry materials via native envmap, IBL/Skylighting providers, Creator tool | Ext: authored sentinel/reflectance selection, FO4 material workflow | Pt | H | S/M | High |
| Effects11 | Core, policy-gated | `.fx` host, color correction, editor, weather/light controls, volumetric rays | Policy: ENB-format preset compatibility | NS | H | L | Med |
| [Exponential Height Fog](deviations/ExponentialHeightFog.md) | Core | Analytic/froxel fog; shadow/environment/weather/secondary consumers; local-light scattering (needs LLF light grid) | N/A: ENB | Pt | H | L | High |
| Extended Materials | Mixed | Object POM + contact refinement*, warping fix*, terrain POM/height blend* | Ext: complex envmask material, terrain height assets, parallax soft shadows | NS | H | M | Med |
| Extended Translucency | Mixed | Alpha models on an existing alpha route* | Ext: per-mesh NIF metadata, world translucent coverage | NS | M | S | Low |
| Grass Collision | Core | Contact field, blade deformation/motion, optimized-grass consumer | — | NS | H | M | Med |
| Grass Lighting | Mixed | Brightness/facing, complex-atlas decode, LOD brightness | Ext: exact specular/transmission/AO, PBR grass. N/A: forward activation | NS | M | S/M | Med |
| Grass Optimizations | Mixed | Culling policy, HiZ, complex detection (no standalone payoff) | Ext: instance/bucket renderer + indirect submission, mesh LOD | NS | H | L | Med |
| Hair Specular | Mixed | Color controls, opaque Marschner, depth self-shadow, single tangent shift* | Ext: Kajiya-Kay frame/dual shift, blended hair | NS | H | M | Med |
| HDR Display | Core | HDR10 output, native tonemap unclamp, UI brightness, SR/FG HDR transport | AutoHDR requires Effects11 | NS | H | L | Med |
| HorizonFix | N/A | — | Integration with a Skyrim-only companion DLL | — | L | — | High |
| IBL | Core | Environment SH, sky SH*, occlusion integration, static IBL, fog, grass/tree | — | NS | H | L | Med |
| Interior Sun | Core (provisional) | Native sun gating*, portal culling, double-sided/distance, gameplay sun | N/A: plain interior directional light | NS | H | L | Low |
| [Inverse Square Lighting](deviations/InverseSquareLighting.md) | Core | Attenuation, radius/lifetime, gameplay luminance, TOML authoring (relocated LIGH fields), editor, diagnostics | — | Pt | H | M | High |
| Light Limit Fix | Mixed | Particle lights, light-limit visualization, 3D light grid as EHF provider | Ext: capacity/shadow-limit redesign. N/A: surface clustered culling (native tiled), strict lights, contact shadows | NS | H | M | Med |
| Linear Lighting | N/A | Shared-color compatibility (done), optional calibration controls | N/A: linearization (FO4 is linear), ENB | Substrate | L | S | High |
| LOD Blending | Core | Terrain/object LOD brightness-gamma, terrain/grass vertex-color removal | N/A: snow-LOD, DistantTree | NS | H | S | High |
| [Performance Overlay](deviations/PerformanceOverlay.md) | Core | FPS/VRAM/frame/draw diagnostics, A/B comparison, family/total toggles, history plots, FG timing labels | — | Pt | H | M | High |
| Remote Control | Core | DevBench C-ABI bridge, settings, inspection/cache, capture commands | — | NS | H | M | High |
| [RenderDoc](deviations/RenderDoc.md) | Core | Capture/targeting, inventory/comments/disk management, UI/hotkeys/annotation | — | P | H | S | High |
| [Screen Space GI](deviations/ScreenSpaceGI.md) | Core | AO, GI, temporal, specular GI, vertex AO (relocated), provider wiring | — | Pt | H | M | High |
| [Screen-Space Shadows](deviations/ScreenSpaceShadows.md) | Core | Bend raymarch, direct/back visibility, tree/alpha/secondary receivers, preview/telemetry | — | Pt | H | M | High |
| Screenshot | Core | SDR/crop/async/clipboard, temporal source, HDR PNG (needs HDR Display) | — | NS | M | M | High |
| Skin | Mixed | Detail normal/color, default dual-lobe*, transmittance*, sweat inputs* | Ext: RFAOS/wet textures, wet-skin film | NS | H | M | Med |
| Sky Sync | Core | Alternate sun path, moon casting*, dawn/dusk + godray dimming | N/A: daytime sun fix (native matches), Skyrim compat checks | NS | M | M | Med |
| Skylighting | Core | Occlusion capture via native precipitation renderer, SH probes, diffuse/specular/shadow visibility, vertex AO (relocated) | — | NS | H | L | Med |
| Subsurface Scattering | Mixed | Separable and Burley on native class-5 skin (single profile), character light* | Ext: per-pixel amount/profile channel | NS | H | M | Med |
| Terrain Blending | Mixed | Depth prep/feather (no standalone payoff) | Ext: lit terrain/object transition (the shippable effect) | NS | H | L | Med |
| Terrain Helper | Mixed | Displacement-slot gather/bind* | Ext: default-land ESP package | NS | M | M | Low |
| [Terrain Shadows](deviations/TerrainShadows.md) | Core | Height-field producer, world receivers, time/transition events, secondary/remaining receivers, diagnostics | — | Pt | H | L | High |
| Terrain Shadows - Heightmaps | Core (data) | FO4 worldspace pack in the existing format | N/A: Skyrim payload | Loader P, no pack | H | M | High |
| Terrain Variation | Core | Landscape stochastic sampling, LOD anti-tiling, landscape-textured meshes, height/PBR coherence | — | NS | H | M | High |
| TruePBR | Mixed | Surface POM on native height* (EM marcher under `TRUE_PBR`+`HasDisplacement`) | Ext: PBR material loader + RMAOS lighting, coat/fuzz/SSS/hair, landscape/grass PBR, glints | NS | H | M | Med |
| Unified Water | Mixed | Optical distance blend, material identity fix*, cache UI | Ext: distant tile generation/lifetime, world flowmap | NS | M | S/M | Med |
| [Upscaling](deviations/Upscaling.md) | Core | SR/native AA, DR/RCAS/reflection/depth consumers, canonical SDK depth, masks, underwater chain, FG/UI/Reflex | — | Pt | H | L | High |
| Volumetric Lighting | Mixed | Native GFSDK enable/quality controls | Ext: full raymarched renderer. N/A: Skyrim dispatch optimization | NS | L | S | Med |
| Volumetric Shadows | Core | VSM producer, DFLight soft sun, Effect/Water short rays, Particle receiver | — | NS | H | L | Med |
| [Water Effects](deviations/WaterEffects.md) | Mixed | Exterior/secondary caustics, interior heights*, debug | Ext: normal-alpha water parallax, flowmap parallax (height assets) | Pt | H | L | High |
| [Wetness Effects](deviations/WetnessEffects.md) | Mixed | Weather accumulation, wet albedo/shore, water-surface rain ripples, shelter | Ext: MRT6 film (normals/roughness/coat), ground ripples/splashes, flowmap ripples | Pt | H | M | High |

[FrameGeneration](deviations/FrameGeneration.md) and [MotionVectorFixes](deviations/MotionVectorFixes.md)
are FO4-only rule-4 core features.

## Slice notes

- **TruePBR / Extended Materials parallax.** TruePBR relief is Extended Materials' marcher
  (`ExtendedMaterialsParallaxCore.hlsli`) gated by `TRUE_PBR`, `EnableParallax` and `HasDisplacement`
  (`Lighting.hlsl:1175-1217`), so it ships as one EM slice in `DFPrepass` before material sampling. The
  slice is core only if FO4 materials carry a native height texture. Composite `PARALLAX_OCCLUSION_MAPPING`
  macros prove emission, not executed relief, and prepass `0x40000` is SKIN_TINT. RE spike first. Terrain
  Variation must share the same UV offsets.
- **Dynamic Cubemaps dry materials.** Sample the DC cube (t34/t35) at the six native probe sites in
  `DFComposite.hlsl`, keeping FO4 gain, SSLR blend and exclusions. Use DC's roughness-to-mip contract, not
  the native probe LOD.
- **Wetness Effects.** `package\Shaders\Water.hlsl` has no `WETNESS_EFFECTS`/`GetRainDrops` consumer; the
  water-surface rain slice is missing.
- **Subsurface Scattering.** Replace FO4's fixed second-composite blur with upstream separable/Burley on
  native class-5 eligibility; never stack both.
- **Cloud Shadows.** Prior art on branch `cloud-shadows` (`3314d567`) maps material pointers to layers;
  replace with typed layer order.
- **Skylighting.** Reuse FO4's precipitation geometry/camera renderer, redirecting it to Skylighting's
  upstream-owned target for direction-sampled captures; stock logical DS8 alone is insufficient.
- **Light Limit Fix.** FO4 tiled lighting (625 lights, 127 per tile) replaces surface clustering; particle
  lights feed the native list within its capacity. The upstream 3D cluster grid is still needed as EHF's
  local-light scattering input.
- **Grass.** Grass wind/collision/placement is already reconstructed in `DFPrepass.hlsl`; remaining stock
  grass variants are port work.
- **Effects11.** ColorCorrection ships alone after the FO4 HDR/tonemap attachment point is found.

## Core port order

Ordered by payoff over effort, respecting dependencies.

1. **Finish ported features:** DC dry materials (S/M); Wetness water-surface rain (M); Terrain Shadows FO4
   heightmap pack (M); Water Effects caustics proof (S).
2. **Cheap new features:** LOD Blending (S); Terrain Variation (M); Remote Control (M; DevBench automation).
3. **Mid-size, few dependencies:** Cloud Shadows (L; feeds EHF); Subsurface Scattering (M); Volumetric
   Shadows producer + DFLight (M); Hair Specular opaque slices (M); Grass Collision (M, actor-bounds RE);
   LLF particle lights + visualization (M).
4. **RE-gated:** native-height POM spike, then Extended Materials POM + TruePBR surface POM + terrain height
   blend; Interior Sun; Extended Translucency alpha models.
5. **Providers:** DC → IBL; Skylighting in parallel; LLF 3D light grid before EHF local-light scattering;
   then provider adapters in DC/SSGI/EHF/Wetness.
6. **Remaining core:** Skin detail/default response; Sky Sync; Grass Lighting basics; Unified Water optical;
   VL controls; CS Editor; Screenshot; HDR Display (L); Effects11 (policy-gated).

## Extension slices

Shipped: Wetness MRT6 film. Candidates: DC authored reflectance/material workflow (pairs with TruePBR
materials); TruePBR full materials; Extended Materials complex materials and parallax shadows; Grass
Optimizations renderer; Unified Water tiles and flowmaps; water parallax height assets; Terrain Blending lit
transition; Terrain Helper ESP; Skin RFAOS/wet film; SSS profiles; Hair Kajiya-Kay and blended hair; LLF
capacity/shadow redesign; full Volumetric Lighting renderer; Extended Translucency world coverage.
