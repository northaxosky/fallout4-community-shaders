# InverseSquareLighting

Rules, Kind legend and cross-cutting records: [README](README.md).

Upstream #2825 merged ISL into Light Limit Fix; the feature keeps its name here. `GetAttenuation`
lives in the shared seam `LightLimitFix/Attenuation.hlsli`. It, `LightLimitFix/Common.hlsli`,
`Common/Game.hlsli` and `Common/Color.hlsli` are staged unchanged. The global replacement, global settings, comparison
view and b7 ISL block are deleted. CPU radius/luminance math matches the pin,
including its zero-radius edge case.

The larger light footprint is an accepted parity cost: upstream `ProcessLight` applies intensity
times four and cutoffs 0.022 (shadow) / 0.05 before LLF clustering uses the calculated radius.
`Lighting.hlsl` rejects ISL attenuation below `1e-5`; restoring the old `0.001` cutoff would truncate
upstream lighting. FO4 publishes that radius before its native culling instead of LLF clustering.

Feature classification: **mixed**. Core (rule 2): upstream inverse-square algorithm and design. Extension (rule 3):
derivation of unauthored lights from native falloff, an FO4-original algorithm.
No Fix is applied to the retained upstream radius defect. Offline gates are not native-hook
or rendering proof.

## Translations

Engine evidence refers to fallout4-re `docs\engine-facts.md`, **Local lights**
and its OG/NG/AE Address Library table.

| Kind | Upstream | Fallout 4 / evidence | Where |
|---|---|---|---|
| Forced | LIGH DATA bits14/15 and falloff/FOV authoring | External `Lights\*.toml` identifies plugin-local LIGH/reference IDs and retains inverse-square/linear/cutoff/size semantics. DATA 0x4000 is nonshadow spot and 0x8000 disables specular; TESForm bits14/15 are temporary/visible-distant; spot FOV/exponent are consumed. No free bits or universally spare float exists | `LightAuthoring.{h,cpp}`, feature README |
| Forced | Skyrim runtime memory overlay | Typed NiLight-owned sidecar keeps preconversion diffuse, animated dimmer, authored cutoff/size, original radius and form/reference identity without overlaying incompatible native storage | `InverseSquareLightingData.{h,cpp}`, `LocalLights.{h,cpp}`; LIGH source offsets / CPU radius/color publication |
| Forced | CreatePointLight form hook | GenDynamic `{30546,2198256,2198256}` and AddLight `{1109421,2317457,2317457}` capture form/reference/NiLight and actual shadow wrapper; FO4 creates NiPointLight even for spot shape6 | `InverseSquareLighting.cpp` `CreateLight`, `AddLight`; Placed LIGH creation |
| Forced | LLF ProcessLight radius before clustering | Update `{1022957,2198261,2198261}` refreshes animated dimmer; TestFrustumCull `{1440624,2318414,2318414}` updates all radius channels before native list admission and invalidates the cached spot cone when radius changes | `UpdateLight`, `CullLight`, `LocalLights.cpp` `InvalidateSpotCone`; Flicker and pulse / CPU frustum sphere and fade |
| Forced | Runtime extension lifetime | Reference RemoveLight `{162205,2200909,2200909}`, scene RemoveLight `{1410391,2317464,2317464}` and native orphan pruning release sidecar ownership and restore original radius. The extra owner must not defeat native sole-owner pruning | `RemoveLight`, `RemoveSceneLight`, `TiledCallback`; Light removal seam / Tiled producer and eligibility; v1 `5c2a9f95` feature README records OG/NG/AE PDB/disassembly verification and NiLight-removal tail call `{1158402,2317463,2317463}` |
| Forced | LLF light buffer/list publication | Tiled callback `{999390,2317525,2317525}` captures provenance; AddLight `{1250844,2318542,2318542}` writes t8 at the exact t6 dense append index/side; UpdateStructuredBuffer `{402301,2276904,2276904}` uploads the corresponding snapshot, not a guessed pointer/form index | `TiledCallback`, `AppendLight`, `UploadLights`, `DFTiledLighting.hlsl`; Tiled index identity / stride48 / Structured upload and bind |
| Forced | Lighting.hlsl clustered and strict light consumers | BSDFLight SetupGeometry `{976849,2319150,2319150}` and SetupPointLightGeometry `{212931,2319153,2319153}` (OG/NG/AE `0x28C37A0/0x20EC4E0/0x22495F0`, the per-draw point-light constant writer that bypasses SetupGeometry) supply b11 for all native local raster families, including shadow/spot/gobo/attenuation-only. Tiled and raster call unchanged ISL and upstream PointLight color; unflagged lights retain native radial/color arithmetic and directional light stays unchanged | `SetupGeometry`, `FO4/InverseSquareLighting{Consumer,Raster}.hlsli`, `DFLight.hlsl`; Raster local constants / CPU radius/color publication |
| Forced | LightingEval `VanillaNormalization` 1/pi after `PointLight` | Native local BRDF (DFLight/DFTiledLighting) has no 1/pi, so the consumer applies it to ISL radiance; `lightGamma` 2.2 mirrors native local-light color power pow(diffuse, 2.2) | `FO4/InverseSquareLightingConsumer.hlsli` `GetColor`, `SharedData.cpp` `PackFeatures`; CPU radius/color publication |
| Forced | Upstream 256-entry cluster list | Native 127-index tile appends are guarded when ISL is contributed across dimensions10–25. Expanded radii cannot write into adjacent tiles; stock compilation remains byte-identical without the feature | `DFTiledLighting/TileCullCS.hlsli`; AE tile culling and capacity |
| Forced | Skyrim BSLight_GetLuminance | FO4 `{170662,2318428,2318428}` calls the same portable attenuation with mean preconversion diffuse × dimmer ×4, preserving ignored/disabled light exclusions, native shape masks and cached luminance write, but not render-only currentFade | `Luminance`, `LocalLights.cpp` `ShapeAttenuation`; Gameplay point attenuation / exclusions / Detection and script consumers |
| Divergence | Lights are ISL only when authored | `derive_unauthored_lights` (default on, restart) derives `inverse_square`, `size` and an intensity scale for every light with no TOML form/reference entry, per instance in GenDynamic from the native falloff `pow(1 - saturate(a + b*x^c), 2.2)`, native radius and the reference's `TESObjectREFR::GetFade` `{141848,2202609,2202609}`. Only upstream-authored inputs are filled; the scale equals an author retuning LIGH fade and size is derived so ISL's half-intensity distance matches the native curve's (clamped to 0.01..<50, 50 being the sqrt2 sentinel). Gain matches the d^2-weighted energy over [0, R] with that size; derived lights keep the native radius. Kept native: DATA 0x4 negated color, b == 0, nonpositive native energy, radius <= 0.1. A TOML entry is authoritative and never merged. FO4 has no authored content. Reference XRDS adds to the radius and XLIG/ExtraLightData adds to the fade (engine-facts Reference overrides / ExtraLightData overrides), so radius and fade differ per reference and the fit is per instance | `LightDerivation.{h,cpp}`, `LocalLights.cpp` `RadialFalloff`, `ReferenceFade`, `CreateLight`; Reference overrides and initial light / ExtraLightData overrides |
| Divergence | Authored lights recompute radius from the animated dimmer every refresh | Derived lights keep the native radius fixed and the animated dimmer scales only `fade`; authored lights keep the upstream per-refresh recompute | `TESObjectLIGH::Update` `{AE 0x4554E0}` writes only `NiLight.dimmer` (+0x144) for flicker/pulse and never changes radius (engine-facts light animation rows), so recomputing reach would slide the smoothstep fade window and over-pulse derived lights | `InverseSquareLightingData.cpp` `UpdateShaderData`, `RuntimeLightData::derived` |
| Framework | Always-loaded feature and JSON host UI | Repository `load=false`, TOML activation, ownership/hash/identity gates, resource readiness, startup compute compilation barrier, exact PS/CS binding restoration, cached telemetry and quarantine radius restoration | `InverseSquareLighting.{h,cpp}`, `ShaderInjection.{h,cpp}`, `FeatureConfigTests.cpp` |

## Not supported

No additional engine limitation is claimed beyond the evidenced translations.
Skyrim's literal record/runtime overlay is replaced, not partially emulated.

## Pending

| Kind | Upstream | Notes / where |
|---|---|---|
| Pending | CSEditor live per-light overrides, hide-regular/hide-ISL diagnostics and Light Placer runtime override publication | TOML supplies authored base/reference semantics, not Skyrim's interactive editor or external runtime placement API. `LightAuthoring` and `DrawSettings` document restart-only authoring; these integrations are not declared equivalent |

