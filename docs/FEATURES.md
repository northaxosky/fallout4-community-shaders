# Features

Classification of every upstream Skyrim Community Shaders feature
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

Status: **Pt** partial, **NS** not started; finished features leave the map. Payoff and effort cover all core slices.

| Feature | Verdict | Core slices | Extension / N/A slices | Status | Payoff | Effort | Conf. |
|---|---|---|---|---|---|---|---|
| [CS Editor](deviations/CSEditor.md) | Core | Weather picker, lock, time controls, weather panels and per-weather feature overrides; template/imagespace/godray editors, light editor and shell pending | N/A: Skyrim VL noise fields | Pt | H | L | High |
| Cloud Shadows | Mixed | Layer-cube producer, DFLight directional receivers, Water sun specular | Ext: Effect/particle sun term (FO4 Effect lighting has none). N/A: legacy VL consumer, BSParticleShader (never drawn) | NS | H | L | Med |
| [Dynamic Cubemaps](deviations/DynamicCubemaps.md) | Mixed | Capture/filter/inference, water, native envmap materials, Skylighting/IBL-specular providers, Creator tool | Ext: authored sentinel/reflectance selection, FO4 material workflow | Pt | H | S/M | High |
| Effects11 | Core | `.fx` host and effect chain, color correction, editor, weather/light controls, volumetric rays, water shore fade/sun specular | N/A: ENB preset/binary compatibility | NS | H | L | Med |
| Exponential Height Fog | Core | Deferred until upstream releases it | — | NS | — | — | — |
| [Extended Materials](deviations/ExtendedMaterials.md) | Mixed | Object POM + contact refinement, warping fix, terrain POM/height blend (height from `_s` alpha); parallax material gate pending | Ext: complex envmask material; sun parallax soft shadows (done). N/A: point-light parallax shadows | Pt | H | M | Med |
| Extended Translucency | Mixed | Alpha models on an existing alpha route* | Ext: per-mesh NIF metadata, world translucent coverage | NS | M | S | Low |
| Grass Collision | Core | Contact field, blade deformation/motion, optimized-grass consumer | — | NS | H | M | Med |
| Grass Lighting | Mixed | Brightness/facing, complex-atlas decode, LOD brightness | Ext: exact specular/transmission/AO, PBR grass. N/A: forward activation | NS | M | S/M | Med |
| Grass Optimizations | Mixed | Culling policy, HiZ, complex detection (no standalone payoff) | Ext: instance/bucket renderer + indirect submission, mesh LOD | NS | H | L | Med |
| Hair Specular | Mixed | Color controls, opaque Marschner, depth self-shadow, single tangent shift* | Ext: Kajiya-Kay frame/dual shift, blended hair | NS | H | M | Med |
| HDR Display | Core | HDR10 output, native tonemap unclamp, UI brightness, SR/FG HDR transport | AutoHDR requires Effects11 | NS | H | L | Med |
| HorizonFix | N/A | — | Integration with a Skyrim-only companion DLL | — | L | — | High |
| [IBL](deviations/IBL.md) | Core | Environment SH, sky SH*, ambient/fog/effect consumers, occlusion integration, specular IBL | N/A: static IBL, world-map toggle, ENB | Pt | H | L | Med |
| Interior Sun | Core (provisional) | Native sun gating*, portal culling, double-sided/distance, gameplay sun | N/A: plain interior directional light | NS | H | L | Low |
| [Inverse Square Lighting](deviations/InverseSquareLighting.md) | Mixed | Attenuation, radius/lifetime, gameplay luminance, TOML authoring (relocated LIGH fields), editor, diagnostics | Ext: derivation of unauthored lights from native falloff | Pt | H | M | High |
| Light Limit Fix | Mixed | Light-limit visualization | Ext: capacity/shadow-limit redesign. N/A: surface clustered culling (native tiled), strict lights, contact shadows, particle lights (no FO4 content) | NS | H | M | Med |
| Linear Lighting | N/A | Shared-color compatibility (done), optional calibration controls | N/A: linearization (FO4 is linear), ENB | Substrate | L | S | High |
| [LOD Blending](deviations/LODBlending.md) | Core | Terrain/object LOD brightness-gamma, terrain/grass vertex-color removal | N/A: snow-LOD | Pt | H | S | High |
| [Performance Overlay](deviations/PerformanceOverlay.md) | Core | FPS/VRAM/frame/draw diagnostics, A/B comparison, family/total toggles, history plots, FG timing labels | — | Pt | H | M | High |
| [Rain Refraction](deviations/RainRefraction.md) | Core | Refractive drop shading from the Dynamic Cubemaps environment, sun shading, motion stretch and transparency; point lights pending (Light Limit Fix) | N/A: Effects 11 preset coupling | Pt | M | S | Med |
| [Remote Control](deviations/RemoteControl.md) | Core | DevBench C-ABI bridge; feature, settings, inspection, shader-cache and RenderDoc capture tools and events; NvPerf tool and screenshot capture pending | — | Pt | H | M | High |
| [Screen Space GI](deviations/ScreenSpaceGI.md) | Core | AO, GI, temporal, specular GI, vertex AO (relocated), provider wiring | — | Pt | H | M | High |
| [Screen-Space Shadows](deviations/ScreenSpaceShadows.md) | Core | Bend raymarch, direct/back visibility, tree/alpha/secondary receivers, preview/telemetry | — | Pt | H | M | High |
| Screenshot | Core | SDR/crop/async/clipboard, temporal source, HDR PNG (needs HDR Display) | — | NS | M | M | High |
| Skin | Mixed | Detail normal/color, default dual-lobe*, transmittance*, sweat inputs* | Ext: RFAOS/wet textures, wet-skin film | NS | H | M | Med |
| Sky Sync | Core | Alternate sun path, moon casting*, dawn/dusk + godray dimming | N/A: daytime sun fix (native matches), Skyrim compat checks | NS | M | M | Med |
| Subsurface Scattering | Mixed | Separable and Burley on native class-5 skin (single profile), character light* | Ext: per-pixel amount/profile channel | NS | H | M | Med |
| [Terrain Blending](deviations/TerrainBlending.md) | Core | Deferred landscape replay, depth-ramp alpha blend into the G-buffer, offset Z-prepass for near terrain | Pending: no-blend mesh flag | Pt | H | L | Med |
| Terrain Helper | Mixed | Displacement-slot gather/bind* | Ext: default-land ESP package | NS | M | M | Low |
| [Terrain Shadows](deviations/TerrainShadows.md) | Core | Height-field producer, world receivers, time/transition events, secondary/remaining receivers, diagnostics | — | Pt | H | L | High |
| [Terrain Variation](deviations/TerrainVariation.md) | Core | Landscape stochastic sampling, LOD anti-tiling, landscape-textured meshes, height/PBR coherence (needs Extended Materials/TruePBR consumers) | — | Pt | H | M | High |
| TruePBR | Mixed | Surface POM (EM marcher under `TRUE_PBR`+`HasDisplacement`; same slice as Extended Materials POM) | Ext: PBR material loader + RMAOS lighting, coat/fuzz/SSS/hair, landscape/grass PBR, glints | NS | H | M | Med |
| [Unified Water](deviations/UnifiedWater.md) | Core | Optical distance blend, distant terrain tile generation/lifetime, cache UI, material identity fix | FO4-specific requirement: preserve vanilla placed-water distance rendering. N/A: Skyrim world flowmap | Pt | M | S/M | Med |
| [Upscaling](deviations/Upscaling.md) | Core | SR/native AA, DR/RCAS/reflection/depth consumers, canonical SDK depth, masks, underwater chain, FG/UI/Reflex | — | Pt | H | L | High |
| Volumetric Lighting | Extension | Native GFSDK enable/quality controls | Ext (planned): godray resolution fix (in-game godrays are pixelated), night-time (moon) godrays; full raymarched renderer. N/A: Skyrim dispatch optimization | NS | H | M | Med |
| [Water Effects](deviations/WaterEffects.md) | Core | Exterior/secondary caustics, interior heights*, debug, water parallax (height from water-normal alpha) | N/A: FLOWMAP parallax (no FO4 flowmap path) | Pt | H | M | High |
| Wetness Effects | — | removed; not ported | — | NS | — | — | — |

[FrameGeneration](deviations/FrameGeneration.md) and [MotionVectorFixes](deviations/MotionVectorFixes.md)
are FO4-only rule-4 core features.

## Slice notes

- **Parallax (Extended Materials / TruePBR).** TruePBR relief is Extended Materials' marcher
  (`ExtendedMaterialsParallaxCore.hlsli`) gated by `TRUE_PBR`, `EnableParallax` and `HasDisplacement`
  (`Lighting.hlsl:1175-1217`), so it ships as one EM slice in `DFPrepass` before material sampling. Vanilla
  textures carry no height, but FO4 parallax texture packs store it in the `_s` (specular/smoothness) alpha,
  which `DFPrepass` already binds at t2 (landscape t8+). Reading it is a relocated upstream datum, so the
  slice is core. No draw identifies a parallax material: the engine drops the BGSM parallax flag and no
  prepass bit or constant carries one, so the host admits objects whose `_s` format stores alpha until the
  BGSM flag is recovered. Terrain shares Terrain Variation's UV offsets.
- **Subsurface Scattering.** Replace FO4's fixed second-composite blur with upstream separable/Burley on
  native class-5 eligibility; never stack both.
- **Cloud Shadows.** Prior art on branch `cloud-shadows` (`3314d567`) maps material pointers to layers;
  replace with typed layer order.
- **Light Limit Fix.** FO4 tiled lighting (625 lights, 127 per tile) replaces surface clustering. Particle
  lights are N/A: upstream lights only texture stems listed in `Data\ParticleLights\*.ini`, which no FO4
  content ships; lighting vanilla glow cards by shader flags would be an FO4-original extension. The
  light-limit visualization (tiles at 127, per-tile count) ships with it.
- **Grass.** Grass wind/collision/placement is already reconstructed in `DFPrepass.hlsl`; remaining stock
  grass variants are port work.
- **Shadow receivers** (fallout4-re `docs/cloud-volumetric-shadows-contract.md`). FO4 Effect LIGHTING carries
  the weather Effect Lighting colour, not sun light, and its PS has no world position; FO4 water has no
  refraction sun term; particles draw through Effect PARTICLES routes. Upstream's Effect/particle shadow
  consumers therefore need an FO4 sun term (extension); water takes shadows on its sun specular only.
- **Effects11.** Replaces ENB's enbeffect role natively; no ENB preset/binary compatibility is kept. Attach at
  FO4's HDR-to-LDR tonemap inside the identified imagespace range (RE); ColorCorrection can ship first.

## Core port order

Ordered by user value over effort, respecting dependencies.

1. **Effects11** (L): highest user value; FO4 users rely on ENB's enbeffect. Starts with tonemap-attachment RE.
2. **Finish ported features:** DC provider adapters (S/M); Water Effects caustics proof (S).
3. **Cheap new features:** LOD Blending (S); Unified Water optical blend and terrain tiles (M); Terrain Variation (M).
4. **Parallax:** Extended Materials object/terrain POM + TruePBR surface POM, reading `_s` alpha (M).
5. **Mid-size, few dependencies:** Cloud Shadows (L); Subsurface Scattering (M); Volumetric
   Shadows producer + DFLight (M); Hair Specular opaque slices (M); Grass Collision (M, actor-bounds RE).
6. **RE-gated:** Interior Sun; Extended Translucency alpha models.
7. **Providers:** LLF light-limit visualization; then provider adapters in DC/SSGI.
8. **Remaining core:** Skin detail/default response; Sky Sync; Grass Lighting basics; CS Editor; Screenshot (then Remote Control screenshot capture); Remote Control NvPerf; HDR Display (L).

## Extension slices

Planned: Volumetric Lighting as an FO4 extension over native GFSDK godrays: enable/quality controls,
a resolution fix for pixelated godrays and night-time (moon) godrays; RE first (GFSDK grid/quality/upsample
parameters, night gating). Not planned: DC authored reflectance/material workflow; LLF vanilla glow-card particle lights. Candidates: TruePBR
full materials; Extended Materials complex materials (envmask-alpha height, RGB metal/colour); Grass Optimizations renderer;
Terrain Helper ESP; Skin RFAOS; SSS profiles; Hair Kajiya-Kay and blended hair; LLF capacity/shadow
redesign; full raymarched Volumetric Lighting renderer; Extended Translucency world coverage.
