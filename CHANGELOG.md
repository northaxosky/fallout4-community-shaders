# Changelog

## 0.5.0

- Subsurface Scattering: new feature ported from Skyrim CS; blurs skin with separable or Burley scattering instead of the vanilla blur (which returns when the feature is disabled); it also turns the vanilla character light off unless enabled in its settings.
- Terrain Blending: new feature ported from Skyrim CS; blends terrain into objects that sink into it by depth, removing the hard seam; no per-mesh opt-out yet.
- Volumetric Shadows: new feature ported from Skyrim CS; softens the sun's soft, rim and back lighting and shadows the sun on water and effects; Skylighting's sky visibility drives those lobes instead while it is enabled.
- When shaders take over a second to compile, Continue, Load, and New Game wait behind the main menu's loading spinner until they finish, with a progress window when DearModdingUI is installed; set `background_compilation` in `[shader_cache]` (restart required) to play while they compile, with uncompiled shaders rendering as stock. Every stock shader variant a loaded feature modifies now precompiles at startup instead of as the game first draws it.
- Shader cache: `max_size_mb` in `[shader_cache]` caps the disk cache (default 4096 MiB); least recently used shaders are removed first.
- Community Shaders now disables all hooks and features and shows a message when the standalone Motion Vector Fixes (`MotionVectorFixes.dll`) or Dynamic Cubemaps F4 (`DynamicCubemapF4.dll`) plugin is installed; remove them, as Community Shaders includes both.
- Frame annotations: `diagnostics.frame_annotations` (Advanced > Diagnostics, restart required) names engine passes, draws and render targets in GPU captures; RenderDoc captures include every event even if the live toggle is off.
- Performance Overlay: added DeferredComposite timing; A/B results read Valid or Marginal only with samples from both variants.
- Performance Overlay: GPU timings no longer overwrite pending queries, and a pass timed several times in one frame counts as one sample.
- Profiling page and per-feature Profiling sections with Skyrim CS's stacked GPU/CPU pass graph and sortable heat tables; the overlay's CS Render Passes view uses the same graph.
- Dynamic Cubemaps: `material_reflections` now defaults to 0.25; interior water reflects only the live cubemap, and water without the Reflections flag now reflects it too.
- Image Based Lighting: now applies in interiors; the `DisableInInteriors` setting is removed.
- Skylighting: fixed a bright patch around the player with 1 or 3 sun shadow cascades (`iDirShadowSplits`); only 2 were supported before.
- Skylighting: specular and sun soft-shadow sampling now clamp to the probe volume's height as in Skyrim CS, fixing a specular edge at that height.
- Inverse Square Lighting: a derived light radius of zero now falls back to 1, as an undefined one already did.
- LOD Blending: disabling the feature no longer normalizes landscape vertex colors or applies LOD brightness and gamma.
- RenderDoc: the capture list, size and Clear All Captures only touch `.rdc` files in the capture directory.
- CS Editor: unlocking the weather keeps the current weather instead of picking a new one.
- Presets now save and apply every feature's settings.
- Settings: unknown keys and sections in `FO4CommunityShaders.toml` are logged as warnings at startup and on reload, and still ignored.
- Feature shaders now receive their inputs on every engine draw route, including instanced, dynamic, particle and command-buffer draws, not only plain triangle-shape draws.
- Disabling a feature's `enabled` toggle now skips its shader lookups for Skylighting, Screen Space Shadows, Terrain Shadows, Volumetric Shadows, Water Effects, Extended Materials parallax shadows and LOD Blending.
- Lower CPU cost per injected draw: bindings are cached, Extended Materials skips unchanged target rebinds, draw counters run only with telemetry on, and per-shader timings run only while the Performance Overlay needs them.
- Fixed an intermittent crash at startup while shaders compile.

## 0.4.1

- Dynamic Cubemaps: far LOD water reflections no longer scramble while walking; adds an Engine Cube Size setting.
- The menu groups features into Skyrim CS's categories (Display, Utility, Lighting, Materials, Landscape & Textures, Water).
- Upscaling: ReShade (standard `dxgi.dll` install) now works with Upscaling and Frame Generation; effects apply before the UI, are included in generated frames, and receive depth.
- Fixed a startup crash on Direct3D 11 feature-level 11.0 GPUs.
- Upscaling: DLSS no longer falls back to TAA until restart after fast travel or entering and leaving interiors.
- Unified Water: interiors no longer show a stray water plane.

## 0.4.0

- Skylighting: new feature ported from Skyrim CS. Scales ambient diffuse and specular lighting by sky visibility, selects the dynamic cubemap specular by sky visibility, applies sky visibility to water reflections and adds probe and occlusion debug views; it has a live enable toggle.
- Extended Materials: ported Skyrim CS's parallax and terrain height blending, with sun parallax shadows on objects and terrain; object parallax needs a height map in the `_s` alpha channel, and flat height maps are ignored.
- Unified Water: ported Skyrim CS's distance-unified water and distant terrain water tiles with per-cell water types and a live enable toggle; placed water keeps vanilla distance rendering.
- Image Based Lighting: ported Skyrim CS's spherical-harmonics ambient, specular and fog tint from the environment and sky cubemaps, with weather profiles; requires Dynamic Cubemaps, off until `EnableIBL` is set; static IBL is not included.
- Terrain Variation: ported Skyrim CS's stochastic terrain sampling, LOD terrain tiling fix and landscape-textured mesh support, with a live enable toggle; mesh support covers natural landscape textures only, and Extended Materials and TruePBR height and RMAOS consumers are pending.
- Dynamic Cubemaps: water reflects the live cubemap near and the engine cube far, with Skyrim CS's screen-space reflection blend; LOD is rendered into the engine reflection cube (`engine_cube_lod`, default on), new debug views show the cube, and all mips are encoded as BC6H.
- Screen-Space Reflections: water and glossy reflections now appear; SSGI no longer disables the SSLR depth pyramid, and SSLR is correct under DLSS/FSR.
- Inverse Square Lighting: lights without authored data now derive their size from the native falloff shape, light color matches Skyrim CS, each draw uses its own light data, and derived light reach no longer changes with animated dimmers.
- CS Editor: ported Skyrim CS's weather picker, weather lock, time controls and weather overrides; override drags persist.
- Remote Control: added a DevBench bridge that publishes events and lets external tools read and change settings, select debug views live, and request RenderDoc captures.
- Terrain Shadows: ships heightmaps for the base-game and DLC worldspaces; other worldspaces still need an xLODGen export.
- Performance Overlay: starts hidden each session; press F10 (rebindable) to show it, as in Skyrim CS. The frametime graph now scales with resolution and no longer spikes when the menu opens, and A/B deltas are signed as in Skyrim CS.
- Upscaling: added NVIDIA Reflex controls (Low Latency Mode, Boost, Use Markers To Optimize, FPS Limit) as in Skyrim CS; Low Latency is on by default and always forced on under DLSS Frame Generation, off under FSR Frame Generation.
- Upscaling: fixed a faint vertical seam at DLSS/FSR Quality; the Pip-Boy and companion map now render unscaled, DLSS no longer resets every frame while paused with Frame Generation, and the Frame Generation proxy device is created correctly.
- Frame Generation: updated Streamline to fix a black screen when loading a save with FSR 4 frame generation enabled and Gamma 2.2 color inputs.
- Water: the shore blend no longer fringes first-person hands and weapons.
- Screen Space GI: history now resets when the render size changes, as in Skyrim CS.
- Diamond City and other open-sky worldspaces are no longer treated as interiors.
- Hooks now use Microsoft Detours, abort cleanly if installation fails and log other mods' hooks on the same targets.
- Shader cache: cached shaders are revalidated after plugin updates.
- RenderDoc: captures write one file per frame with names that stay unique across game sessions.
- The menu's settings pages have sections, plots and reset indicators.
- Every feature now has a live `enabled` toggle beside `load`, except Frame Generation, Inverse Square Lighting, Performance Overlay, RenderDoc and Upscaling.
- Settings: unknown keys are dropped when a feature's config is rewritten; the Performance Overlay `Position` and `PositionSet` keys are ignored, as the menu saves window layout itself; Frame Generation `detailed_diagnostics` applies on the next launch.
- Exponential Height Fog: removed until Skyrim CS releases it.

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
