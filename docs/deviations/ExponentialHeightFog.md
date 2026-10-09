# ExponentialHeightFog

Rules, Kind legend and cross-cutting records: [README](README.md).

All seven `ExponentialHeightFog` shaders, plus their Random, Color, Shading, IBL and Skylighting
includes, are staged unchanged. The ramp-derived FO4 kernel and its b7 block are deleted.
The 192-byte settings block occupies upstream b6 offset 992; feature-owned b0 volume constants
are 400 bytes. RGBA16F material/scattering/history/integration volumes, R32_FLOAT depth histories,
four dispatches, Halton bases 2/3/5, slice mapping, history weights and sampling retain the pin.

Feature classification: **core** (rule 2: upstream fog algorithm and design). Pending rows are unfinished porting
or verification work, not engine incompatibilities or a claim of full enabled-feature parity.

## Translations

| Kind | Upstream | Fallout 4 | Why / evidence | Where |
|---|---|---|---|---|
| Forced | Skyrim camera/depth input and compute b12 | Fresh copied world-camera records populate feature-owned b4; canonical t17 supplies world-projection depth; native b12 remains engine-owned | fallout4-re engine-facts Camera-cache ownership, Main camera preparation, Native composite depth partition; native first-person depth uses a different projection | `VolumetricFog.cpp` `UpdateCamera`, `FO4/ExponentialHeightFogConsumer.hlsli`, `CanonicalDepth.cpp` |
| Forced | Deferred prepass builds the fog volume before lighting | Prepare settings before shared b6 publication; dispatch after canonical depth and terrain preparation, before native deferred lights | FO4 deferred order and world-camera availability in engine-facts; native light/composite consumers need current-frame volume data | `ExponentialHeightFog.cpp` `Load`, `PrepareFrame`, `RenderFrame` |
| Forced | Screen-space composite and transparent fog consumers | Replace six reconstructed native fog body families once; effect alpha/add/multiply blends, water surface/LOD and distant trees call unchanged upstream fog functions | `DFComposite.hlsl` native color/opacity blends; `Effect.hlsl`, `Water.hlsl`, `DistantTree.hlsl` native forward fog equations; static family attribution in fallout4-re `composite-2d-fog-family-attribution.md` is not a live-draw proof | Named engine shaders; `FO4/FogConsumer.hlsli` (`FO4_FOG_REPLACE`, `FO4_FOG_REPLACE_FORWARD`), `FO4/ExponentialHeightFogConsumer.hlsli` |
| Forced | Fog sees the already-composed main-view sky | Exclude sky depth from composite EHF; fog the final logical MainTemp sky once after native forward sky, preserving geometry and inactive allocation pixels | FO4 sky group follows deferred composite; `RenderHooks.cpp` post-forward-sky boundary and existing DynamicCubemaps capture at logical MainTemp. Per-layer affine fog cannot preserve additive sun/stars or mask composition | `VolumetricFog.cpp` `PrepareSky`, `CompositeSky`; `FO4/ExponentialHeightFog/SkyCompositeCS.hlsl` |
| Forced | Sunlight attenuation in upstream lighting consumers | Attenuate all directional BSDFLight families and separate wet-coat sun lobes; leave ambient/local light and independent shadow alpha untouched | engine-facts Raster light accumulation / Base composite equation: FO4 stores direct light separately from ambient and surface composition | `DFLight.hlsl`, forward consumers, lit Effect (README, Effect lighting) |
| Forced | Native fog color passed to upstream `originalFogColorAmount` | Pass reconstructed near/far, low/high native color before sun/grayscale coloration | Six native composite bodies explicitly evaluate these colors; this proves the shader boundary, not the TESWeather-to-b12 uploader | `DFComposite.hlsl`, water/effect/tree consumers |
| Forced | Upstream `SampColorSampler` and volume t19 | Composite uses linear-clamp s13; forward consumers use s15; volume persists at t19; producer CS snapshots preserve temporary provider inputs | Native composite s14 is occupied by scene color. Fog and terrain use the same linear-clamp s13 descriptor; neither consumer family guarantees it natively | `ExponentialHeightFog.cpp`, `VolumetricFog.cpp` `ScatteringInputs`; consumer include |
| Forced | Skyrim weather form identity/transition | FO4 `Sky.currentWeather`, `lastWeather`, `currentWeatherPct` select normalized plugin-local weather profiles | Typed CommonLibF4 `Sky` fields and existing `SnapshotWeather`; light plugins use 12-bit local IDs and ordinary plugins use 24-bit local IDs | `ExponentialHeightFog.cpp` `PrepareFrame`, `World/Weather.cpp` |
| Framework | Upstream JSON, ImGui and startup loading | Retain load=false, ownership/delivery validation, TOML deltas, live settings, forwarding-only UI, fog-factor debug view through host b7 and telemetry | Repository activation, persistence, UI, measured stock identity and fail-closed contracts; colors extend the existing typed float-array mechanism | `ExponentialHeightFog.{h,cpp}`, `ExponentialHeightFogSettings.h`, settings schema/registry |
| Framework | Weather variable registry integration | TOML weather open-map uses the upstream 23 variable names, opt-in `__enabled`, float/RGBA interpolation, integer switching above 0.5 and missing-key user-setting fallback | Repository typed TOML contract replaces upstream JSON; declared settings/defaults/edit ranges remain unchanged | `World/WeatherVariableRegistry.h`, `ExponentialHeightFogSettings.h`, `SettingsRegistry.h` |
| Framework | Failure handling | Camera, substrate, sky-resource or nonfinite dispatch-input failure keeps native fog; failed volume allocation can retain analytic fog; frame-count gaps and resource changes invalidate volume history | Repository runtime-safety contract; compute detaches/restores OM and restores b4–b7/t17 plus owned high slots. The pinned slice formula is unchanged; its singularity is an upstream PR candidate | `ExponentialHeightFog.cpp`, `VolumetricFog.cpp`, `ComputeOMScope`, `ScopedComputeSharedDataBinding` |

## Not supported

| Kind | Upstream | Why / evidence | Where |
|---|---|---|---|
| Framework | Effects11/ENB compatibility suppression | ENB is forbidden by the repository compatibility contract; no ENB provider or compatibility setting is added | `AGENTS.md`; existing host admission |

## Pending

| Kind | Upstream | Notes / code boundary |
|---|---|---|
| Pending | Directional-shadow in-scattering | Shadow SRV lifetime, world-to-shadow transforms and complete 1/2/3-cascade adaptation need proof. `VolumetricFog.cpp` leaves the directional-shadow flag unset; no screen-space mask substitutes for a froxel shadow. See fallout4-re `skylighting-shadow-cascade-contract.md` and `godrays-volumetric-lighting.md` |
| Pending | Native weather fog uploader and fade brightness | TESWeather fog arrays do not prove b12 producer semantics. Native color is mapped at the verified shader boundary; no ramp-to-density fit or invented fade is used. `respectVanillaFogFade` is persisted but has no validated FO4 fade input; sky original-fog color also lacks a validated per-frame source |
| Pending | Analytic DynamicCubemaps input | Existing FO4 cubemap helpers/providers are not the converted upstream consumer contract. The fog include temporarily excludes `DYNAMIC_CUBEMAPS` and restores the surrounding engine feature define; `useDynamicCubemaps`, tint and mip settings retain upstream values without inventing a substitute |
| Pending | IBL, CloudShadows and clustered local lights | Includes are unchanged, but absent host providers stay unbound and their flags/defines stay off. World-point irradiance, cloud transmittance and volumetric light clusters cannot be replaced by surface diffuse/AO/tile buffers. Terrain's validated t60 provider and the Skylighting probe array (t50, flag bit 8, bound only while Skylighting is healthy; `skylighting_frames` telemetry) are connected |
| Pending | Weather editor/reset and full weather-system lifecycle | Profiles are authored in TOML; upstream weather-editor reset actions and per-weather UI are not ported. Registry reset defaults for density, original color and vanilla suppression differ from Settings defaults; user-setting fallback uses the latter |
| Pending | Map/reflection/secondary-view and forward first-person coverage | The three BSLighting families remain stock: secondary views need their own camera/depth publication. Engine-facts BSLighting forward-pass source limits these passes to modes 0/21. Upstream map-menu suppression remains unvalidated. Forward raw near depth retains native fog until a first-person-to-world camera adapter exists; deferred near depth already uses canonical t17 |
| Pending | Runtime route pairing, histories and sky output | Live native fog suppression, MainTemp destination consumption, sky/additive layers, previous-frame origins, dynamic-resolution edges, allocation/dispatch timing and disabled-feature behavior need a build-pinned RenderDoc capture |
| Pending | Native godray coexistence | Native NVIDIA godrays execute later and are not the upstream four-pass volume pipeline. Visual/light-energy overlap and an explicit coexistence policy need runtime evidence; native godrays are not silently disabled |

## Upstream PR candidates

- `src/Features/ExponentialHeightFog.cpp:653–668,709–718`: weather-variable reset defaults
  (`originalFogColorAmount=1`, `fogDensity=0.02`, `disableVanillaFog=false`) disagree with
  `ExponentialHeightFog.h` Settings defaults (`0`, `0.005`, `true`). Confirm intent upstream
  and align reset behavior or document the distinction. No shared code is changed.
- `src/Features/ExponentialHeightFog.cpp:457–465`: the slice denominator is zero when
  `volumetricFogDistance = volumetricFogStartDistance + 9.5` above the camera near plane
  (for example, start 990.5 and distance 1000, both accepted settings). Clamp the far
  boundary beyond the offset near plane upstream to avoid singular/reversed intervals.
  FO4 retains the formula and rejects nonfinite dispatch data through its safety contract.

