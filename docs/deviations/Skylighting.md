# Skylighting

Rules, Kind legend and cross-cutting records: [README](README.md).

Pin: shared `ca9e8a74b` (upstream `bff82b03e` plus the zenith/fade fix). Consumed unchanged:
`features/Skylighting/Shaders/Skylighting/Skylighting.hlsli` and `UpdateProbesCS.hlsl`; the port
does not stage `Skylighting.ini`. Settings keys, defaults and ranges match the pin: `MaxZenith`
(radians, 0 to 90 degrees, default 90), `MinDiffuseVisibility` and `MinSpecularVisibility`
(0.01-1, default 0.1). The b6 `SkylightingSettings` block defaults to a far `PosOffset` and
visibility 1, so an unpublished or unhealthy block reads unit SH and visibility 1.

Feature classification: **core** (rule 2). Pending rows remain unfinished and do not establish
upstream parity.

## Translations

| Kind | Upstream | Fallout 4 / evidence | Where |
|---|---|---|---|
| Framework | Host activation, settings, UI | `load = false` activation, TOML persistence and forwarding-only DearModdingUI. Typed UAV load support for `R16G16B16A16_FLOAT`, `R8_UINT` and `R8_UNORM` is checked in `OnD3D11Ready`; failure quarantines the feature | `Skylighting.cpp`, `SkylightingSettings.h` |

## Pending

| Kind | Upstream | Remaining work / evidence | Where |
|---|---|---|---|
| Pending | P1 capture producer | Precipitation occlusion capture, quadrant frustum, occluder passes, occlusion depth resource | `RenderHooks`, `PrecipitationOcclusion`, `OccluderPasses` |
| Pending | P2 probe grid and lifecycle | Probe/accum/bitmask/visibility textures, grid advance, update dispatch, b6 publication, `Rebuild Skylighting` button, loading-screen reset, interior predicate | `Skylighting.cpp`, `FeatureBuffer.cpp`, `SharedData.cpp` |
| Pending | P3 consumers | `SKYLIGHTING` define, forward/deferred diffuse and specular consumers, `FeatureShaderDeclarations.h` entry | `package/Shaders/` |
| Pending | P4 deferred lighting coverage | Directional-shadow split and ambient addends in DFLight | `package/Shaders/` |
| Pending | P5 specular and provider integration | Specular visibility, SSGI/IBL/DynamicCubemaps provider interaction, water | `package/Shaders/` |
| Pending | P6 shadow cascades | Cascade copy, `DirectionalShadowLightData` buffer (272 B stride) and shadow visibility probes | `Skylighting.cpp`, `ShadowLightData.h` |
