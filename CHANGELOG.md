# Changelog

## 0.2.1

- Imagespace post-processing uses the game's own shaders except for SSLR raytracing, whose baseline reconstruction is byte-identical to stock.
- Dynamic Cubemaps: added upstream's live Screen Space Reflections toggle.
- Upscaling: SSR now uses the reconstructed shader with dynamic-resolution adjustment; removed the machine decompile and shader-patching hook.

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
