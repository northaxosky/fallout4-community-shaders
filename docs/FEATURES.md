# Features

Classification of every upstream Skyrim Community Shaders feature
for Fallout 4, with port order. Per-feature deviation records live in [deviations](deviations/README.md).

## Classification

A feature is split into **slices**: separable passes, effects or shader paths with a named upstream boundary.
Each slice is classified; a feature is **port**, **extension**, **mixed** (port slices plus an extension
remainder) or **N/A**.

1. Non-visual tooling and diagnostics are always ports.
2. A visual slice is a port when it keeps upstream's algorithm and design. Translation never makes it an
   extension: hook anchors, logical render-target/slot mapping, cbuffer layouts, consumers in reconstructed
   stock shaders, data FO4 already produces read through a typed accessor, calibrated defaults, toggles,
   diagnostics, and relocating an upstream datum (same meaning) into a different FO4 container.
3. A visual slice is an extension when FO4 needs machinery upstream does not have: a new datum or transport
   across passes (typically because upstream shades forward and FO4 is deferred), an authoring/asset/metadata
   pipeline, a replaced engine subsystem, or an FO4-original algorithm.
4. Upscaling, FrameGeneration and MotionVectorFixes are ports (doodlum FO4 release lineage).

Stock-shader reconstruction and missing reverse engineering are port work, not extension reasons.
`*` marks a port slice that is conditional on a named RE question. N/A means the problem is absent or FO4
already does it natively.

Calibration: Dynamic Cubemaps' dry-material reflections reuse FO4's native envmap selection (composite
`ENVMAP`, G-buffer probe slice/strength, native gain) and swap only the sampled cube: a small adapter, so a port.
Upstream's 1×1 sentinel cubes cannot register in FO4 (t8 rejects cubes under 128 px), so authored
reflectance selection is an extension.

## Map

Status: **Pt** partial, **NS** not started; finished features leave the map. Payoff and effort cover all port slices.

| Feature | Verdict | Port slices | Extension / N/A slices | Status | Payoff | Effort | Conf. |
|---|---|---|---|---|---|---|---|
| [CS Editor](deviations/CSEditor.md) | Port | Weather picker, lock, time controls, weather panels and per-weather feature overrides; template/imagespace/godray editors, light editor and shell pending | N/A: Skyrim VL noise fields | Pt | H | L | High |
| [Cloud Shadows](deviations/CloudShadows.md) | Mixed | Layer-cube producer, DFLight directional receivers, Water sun specular; cloud self-shadowing pending | Ext: Effect/particle sun term. N/A: legacy VL consumer, BSParticleShader (never drawn) | Pt | H | L | High |
| [Dynamic Cubemaps](deviations/DynamicCubemaps.md) | Mixed | Capture/filter/inference, water, native envmap materials, Skylighting/IBL-specular providers, Creator tool | Ext: authored sentinel/reflectance selection, FO4 material workflow | Pt | H | S/M | High |
| Effects11 | Port | `.fx` host and effect chain, color correction, editor, weather/light controls, volumetric rays, water shore fade/sun specular | N/A: ENB preset/binary compatibility | NS | H | L | Med |
| Exponential Height Fog | Port | Analytic height fog; volumetric fog (froxel in-scatter with transmittance from sun cascades, Terrain/Cloud Shadows, Skylighting, local lights); waits for upstream Physical Sky | — | NS | H | L | Med |
| [Extended Materials](deviations/ExtendedMaterials.md) | Mixed | Object POM + contact refinement, warping fix, terrain POM/height blend (height from `_s` alpha); parallax material gate pending | Ext: complex envmask material; sun parallax soft shadows (done). N/A: point-light parallax shadows | Pt | H | M | Med |
| Extended Translucency | N/A | — | N/A: FO4 never forward-renders lit alpha-blended world geometry; vanilla has no translucent lit fabric | — | L | — | High |
| Grass Collision | Port | Contact field, blade deformation/motion, optimized-grass consumer | — | NS | H | M | Med |
| Grass Lighting | Mixed | Brightness/facing, complex-atlas decode, LOD brightness | Ext: exact specular/transmission/AO, PBR grass. N/A: forward activation | NS | M | S/M | Med |
| Grass Optimizations | Mixed | Culling policy, HiZ, complex detection (no standalone payoff) | Ext: instance/bucket renderer + indirect submission, mesh LOD | NS | H | L | Med |
| Hair Specular | Mixed | Color controls, opaque Marschner, depth self-shadow, single tangent shift* | Ext: Kajiya-Kay frame/dual shift, blended hair | NS | H | M | Med |
| HDR Display | Port | HDR10 output, native tonemap unclamp, UI brightness, SR/FG HDR transport | AutoHDR requires Effects11 | NS | H | L | Med |
| HorizonFix | N/A | — | Integration with a Skyrim-only companion DLL | — | L | — | High |
| [IBL](deviations/IBL.md) | Port | Environment SH, sky SH*, ambient/fog/effect consumers, occlusion integration, specular IBL | N/A: static IBL, world-map toggle, ENB | Pt | H | L | Med |
| Interior Sun | Port (provisional) | Native sun gating*, portal culling, double-sided/distance, gameplay sun | N/A: plain interior directional light | NS | H | L | Low |
| [Light Limit Fix](deviations/LightLimitFix.md) | Mixed | Forward cluster grid, strict lights, ISL attenuation, radius/lifetime, gameplay luminance, effect and water consumers, clustered light count visualization, TOML authoring (relocated LIGH fields), editor, diagnostics; local shadow copy pending | Ext: derivation of unauthored lights from native falloff. N/A: surface clustered shading (native tiled) and its Light Limit and Strict Lights Count views, particle lights (no FO4 content), local shadow cache/scheduler (no FO4 shadow-caster cap), Shadow Mask view (no FO4 shadow mask) | Pt | H | M | High |
| Linear Lighting | N/A | Shared-color compatibility (done), optional calibration controls | N/A: linearization (FO4 is linear), ENB | Substrate | L | S | High |
| [LOD Blending](deviations/LODBlending.md) | Port | Terrain/object LOD brightness-gamma, terrain/grass vertex-color removal | N/A: snow-LOD | Pt | H | S | High |
| [Performance Overlay](deviations/PerformanceOverlay.md) | Port | FPS/VRAM/frame/draw diagnostics, A/B comparison, family/total toggles, history plots, FG timing labels | — | Pt | H | M | High |
| [Remote Control](deviations/RemoteControl.md) | Port | DevBench C-ABI bridge; feature, settings, inspection, shader-cache and RenderDoc capture and NvPerf tools and events; screenshot capture pending | — | Pt | H | M | High |
| [Screen Space GI](deviations/ScreenSpaceGI.md) | Port | AO, GI, temporal, specular GI, vertex AO (relocated), provider wiring | — | Pt | H | M | High |
| [Screen-Space Shadows](deviations/ScreenSpaceShadows.md) | Port | Bend raymarch, direct/back visibility, tree/alpha/secondary receivers, preview/telemetry | — | Pt | H | M | High |
| Screenshot | Port | SDR/crop/async/clipboard, temporal source, HDR PNG (needs HDR Display) | — | NS | M | M | High |
| Skin | Mixed | Detail normal/color, default dual-lobe*, transmittance*, sweat inputs* | Ext: RFAOS/wet textures, wet-skin film | NS | H | M | Med |
| [Sky Sync](deviations/SkySync.md) | Port | Alternate sun path, moon casting*, dawn/dusk + godray dimming, celestial transitions | N/A: Skyrim compat checks | Pt | M | M | Med |
| Subsurface Scattering | Mixed | Separable and Burley on native class-5 skin (single profile), character light* | Ext: per-pixel amount/profile channel | NS | H | M | Med |
| [Terrain Blending](deviations/TerrainBlending.md) | Port | Deferred landscape replay, depth-ramp alpha blend into the G-buffer, offset Z-prepass for near terrain | Pending: no-blend mesh flag | Pt | H | L | Med |
| Terrain Helper | Mixed | Displacement-slot gather/bind* | Ext: default-land ESP package | NS | M | M | Low |
| [Terrain Shadows](deviations/TerrainShadows.md) | Port | Height-field producer, world receivers, time/transition events, secondary/remaining receivers, diagnostics | — | Pt | H | L | High |
| [Terrain Variation](deviations/TerrainVariation.md) | Port | Landscape stochastic sampling, LOD anti-tiling, landscape-textured meshes, height/PBR coherence (needs Extended Materials/TruePBR consumers) | — | Pt | H | M | High |
| TruePBR | Mixed | Surface POM (EM marcher under `TRUE_PBR`+`HasDisplacement`; same slice as Extended Materials POM) | Ext: PBR material loader + RMAOS lighting, coat/fuzz/SSS/hair, landscape/grass PBR, glints | NS | H | M | Med |
| [Unified Water](deviations/UnifiedWater.md) | Port | Optical distance blend, distant terrain tile generation/lifetime, cache UI, material identity fix | FO4-specific requirement: preserve vanilla placed-water distance rendering. N/A: Skyrim world flowmap | Pt | M | S/M | Med |
| [Upscaling](deviations/Upscaling.md) | Port | SR/native AA, DR/RCAS/reflection/depth consumers, canonical SDK depth, masks, underwater chain, FG/UI/Reflex | — | Pt | H | L | High |
| [Volumetric Lighting](deviations/VolumetricLighting.md) | Extension | — | Ext: native GFSDK godray controls and output-relative resolution (`feat/volumetric-lighting`), night-time (moon) godrays. N/A: Skyrim froxel VL patches (FO4 has no froxel renderer; volumetrics come from Exponential Height Fog) | NS | M | M | Med |
| [Water Effects](deviations/WaterEffects.md) | Port | Exterior/secondary caustics, interior heights*, debug, water parallax (height from water-normal alpha) | N/A: FLOWMAP parallax (no FO4 flowmap path) | Pt | H | M | High |
| Wetness Effects | — | removed; not ported | — | NS | — | — | — |

[FrameGeneration](deviations/FrameGeneration.md) and [MotionVectorFixes](deviations/MotionVectorFixes.md)
are FO4-only rule-4 port features.

## Slice notes

- **Parallax (Extended Materials / TruePBR).** TruePBR relief is Extended Materials' marcher
  (`ExtendedMaterialsParallaxCore.hlsli`) gated by `TRUE_PBR`, `EnableParallax` and `HasDisplacement`
  (`Lighting.hlsl:1175-1217`), so it ships as one EM slice in `DFPrepass` before material sampling. Vanilla
  textures carry no height, but FO4 parallax texture packs store it in the `_s` (specular/smoothness) alpha,
  which `DFPrepass` already binds at t2 (landscape t8+). Reading it is a relocated upstream datum, so the
  slice is a port. No draw identifies a parallax material: the engine drops the BGSM parallax flag and no
  prepass bit or constant carries one, so the host admits objects whose `_s` format stores alpha until the
  BGSM flag is recovered. Terrain shares Terrain Variation's UV offsets.
- **Subsurface Scattering.** Replace FO4's fixed second-composite blur with upstream separable/Burley on
  native class-5 eligibility; never stack both.
- **Light Limit Fix.** Opaque surfaces keep FO4's native tiled lighting (625 lights, 127 per tile). Forward
  consumers (Effect, Water; Rain Refraction pending) read a clustered grid of every nearby light, with
  portal-strict lights bound to rooms. Particle lights are N/A: upstream lights only texture stems listed in
  `Data\ParticleLights\*.ini`, which no FO4 content ships; lighting vanilla glow cards by shader flags would be
  an FO4-original extension. Debug view: clustered light count.
- **Extended Translucency.** Forward BSLightingShader draws only Interface3D and cube captures. Blended
  lit world geometry renders only when decal-flagged, through the `DFPrepass` BLEND variant; material alpha
  below 1 becomes a 4×4 dither. Vanilla skinned blended content is hair/lash head parts (upstream excludes
  them) and decals; glass and see-through fabric are BSEffect.
- **Grass.** Grass wind/collision/placement is already reconstructed in `DFPrepass.hlsl`; remaining stock
  grass variants are port work.
- **Shadow receivers** (fallout4-re `docs/cloud-volumetric-shadows-contract.md`). FO4 Effect LIGHTING carries
  the weather Effect Lighting colour, not sun light, and its PS has no world position; FO4 water has no
  refraction sun term; particles draw through Effect PARTICLES routes. Upstream's Effect/particle shadow
  consumers therefore need an FO4 sun term (extension); water takes shadows on its sun specular only.
- **Effects11.** Replaces ENB's enbeffect role natively; no ENB preset/binary compatibility is kept. Attach at
  FO4's HDR-to-LDR tonemap inside the identified imagespace range (RE); ColorCorrection can ship first.

## Port order

Ordered by user value over effort, respecting dependencies.

1. **Effects11** (L): highest user value; FO4 users rely on ENB's enbeffect. Starts with tonemap-attachment RE.
2. **Finish ported features:** Light Limit Fix local shadow copy (M); DC provider adapters (S/M); Water Effects caustics proof (S).
3. **Cheap new features:** LOD Blending (S); Unified Water optical blend and terrain tiles (M); Terrain Variation (M).
4. **Parallax:** Extended Materials object/terrain POM + TruePBR surface POM, reading `_s` alpha (M).
5. **Mid-size, few dependencies:** Subsurface Scattering (M); Volumetric
   Shadows producer + DFLight (M); Hair Specular opaque slices (M); Grass Collision (M, actor-bounds RE).
6. **RE-gated:** Interior Sun.
7. **Providers:** provider adapters in DC/SSGI.
8. **Remaining ports:** Skin detail/default response; Sky Sync; Grass Lighting basics; CS Editor; Screenshot (then Remote Control screenshot capture); HDR Display (L).

## Extension slices

Not planned: DC authored reflectance/material workflow; LLF vanilla glow-card particle lights. Candidates: TruePBR
full materials; Extended Materials complex materials (envmask-alpha height, RGB metal/colour); Grass Optimizations renderer;
Terrain Helper ESP; Cloud Shadows Effect/particle sun term; Skin RFAOS; SSS profiles; Hair Kajiya-Kay and blended hair.
