# Changelog

## 0.3.1

- Fixed the in-game menu and performance overlay not loading with DearModdingUI 0.2.0. Community Shaders now works with DearModdingUI 0.2.0 and later.

## 0.3.0

- LOD Blending: ported Skyrim CS's LOD terrain and object brightness and gamma controls and terrain vertex-color removal; snow LOD settings are not available.
- Water Effects: ported Skyrim CS's water parallax; it follows the Water Effects `enabled` toggle and leaves vanilla water flat. Caustics now use Skyrim CS's implementation with per-cell water data.
- Exponential Height Fog: now runs Skyrim CS's analytic and volumetric fog pipeline with weather profiles; it has no directional shadow cascade input and no IBL, Skylighting, Cloud Shadows or local-light providers yet.
- Terrain Shadows: now uses Skyrim CS's heightfield shadows and terrain occlusion data; it still needs a user-supplied xLODGen heightmap export.
- Screen Space GI, Dynamic Cubemaps, Inverse Square Lighting, Performance Overlay and RenderDoc now share their implementation with Skyrim CS. The Performance Overlay adds Skyrim CS's profiler and A/B analysis, with GPU timings for every feature pass.
- Dynamic Cubemaps: environment-mapped materials now reflect the live cubemap, blended from the authored cubemap by the new `material_reflections` setting (default 1) and faded out in rain.
- Wetness Effects: removed. Rain-darkened surfaces and wet reflections are no longer provided.
- Screen Space Shadows: now uses Skyrim CS's Bend shaders with a corrected half-pixel lookup; first-person hands and weapons cast shadows as Skyrim CS does, replacing the 0.2.0 exclusion.
- Fixed fog and terrain shadows leaking into secondary-view lighting, and frames without a usable camera or depth now skip temporal processing instead of reusing stale data.
- Shader ownership toggles are editable in Advanced settings and apply immediately, including to feature shader contributions.
- Utility, sky, particle, blood splatter and secondary-view lighting shaders remain stock. Effect and distant tree replacements remain available for fog and terrain-shadow consumers.
- Settings: TerrainShadows replaces `enabled`/`downsample_factor` with `EnableTerrainShadow`; SSGI keys are cased as in Skyrim CS; InverseSquareLighting requires per-light authored opt-in; RenderDoc replaces `multi_frame_count` with `"Capture Frame Count"` and removes `min_free_disk_gib`; the WetnessEffects section is removed.
- Fallout 4 OG 1.10.163: injected shaders now compile and are checked against stock shader output, and Inverse Square Lighting hooks an OG-safe call site. It loads and renders on OG, but only 1.11.240 is officially supported.

## 0.2.1

- Imagespace post-processing now uses the game's own shaders; only screen-space reflections are replaced, with a rebuild that matches vanilla exactly.
- Dynamic Cubemaps: added upstream's Screen Space Reflections toggle, which turns screen-space reflections off on all surfaces.
- Upscaling: screen-space reflections use the rebuilt vanilla shader adjusted for render resolution, replacing a decompiled copy, so they fade at screen edges like vanilla; updated Streamline with a fix for an exception in its app denylist check.

## 0.2.0

- Added an in-game changelog.
- Screen Space Shadows: first-person hands and weapons are no longer darkened.
- Performance Overlay: now shows by default when loaded.
- Screen Space GI: ported upstream's GI integration, denoiser, resolution modes and vanilla SSAO toggle; indirect light now composes in linear space where the game forms diffuse light, respects vertex AO, and has a stronger AO default for interiors lit by placed lights.
- Dynamic Cubemaps: ported upstream's capture pipeline and limited reflections to water and wet surfaces as upstream does, fixing flat silver reflections elsewhere.
- Wetness Effects: ported upstream rain timing, shore wetness, weather puddles and film roughness.
- Upscaling: restored the depth and refraction upscale pass, which was silently skipped.
- Fixed several reconstructed engine shaders that did not match vanilla output, including decals, static objects, grass, tiled lighting and sunlight with Screen Space Shadows disabled.
- RenderDoc: captures the game frame when Frame Generation is loaded, loads with the feature and finds the installed RenderDoc automatically.
- OG and NG runtimes: every engine path now has addresses; still untested, only 1.11.240 is supported.

## 0.1.1

- Fixed a black screen in exclusive fullscreen when Upscaling or Frame Generation was loaded; exclusive fullscreen now keeps native presentation and falls back to TAA without frame generation.
- Noted the borderless windowed requirement on the Upscaling and Frame Generation pages.
- Shaders that fail to compile are no longer recompiled every frame.
- Linux/Proton: warn when Wine's builtin shader compiler is detected; features need a Windows 10 d3dcompiler_47.dll.
- Clarified native HDR mod support: RenoDX and Special K HDR do not work with Upscaling or Frame Generation.

## 0.1.0

- Initial release.
