# Terrain Shadows

Rules, Kind legend and cross-cutting records: [README](README.md).

Both `TerrainShadows/ShadowUpdate.cs.hlsl`
and `TerrainShadows/TerrainShadows.hlsli` are staged unchanged. Native DDS dimensions, R16G16_UNORM
shadow heights, 128-thread scans, componentwise penumbra maxima, one-degree softening, half-texel
offsets, bounded UV, ZBlur, weight-1 full sweeps and weight-0.5 ordinary slices match the pin.
Settings use `EnableTerrainShadow`; no downsampling setting or resize pass remains.
The old v0.2.1 default downsample factor of four was a quality divergence: upstream allocates
and scans the original heightmap dimensions. That additional work is an accepted parity cost.

Feature classification: **core** (rule 2: upstream terrain-shadow algorithm and design).
Pending rows remain unfinished and do not establish upstream parity.

## Translations

| Kind | Upstream | Fallout 4 / evidence | Where |
|---|---|---|---|
| Forced | Camera-relative consumer position plus caller origin | Absolute world position from unchanged b4 FrameBuffer; native b12 is engine-owned (`Water.hlsl`, `cbuffer PerFrame : register(b12)`) | `FO4/TerrainShadowsConsumer.hlsli` |
| Forced | Skyrim light / worldspace accessors | Typed FO4 Sky propagation vector and inherited-land worldspace identity; engine-facts Sun light orientation, `TESWorldSpace::GetParentWorld(kLand)` | `TerrainShadows.cpp`, `World/Sky.cpp` |
| Forced | Skyrim native directional consumers | Reconstructed BSDFLight, BSDistantTree, BSWater and lit BSEffect directional terms; point/ambient terms stay separate. FO4's deferred and forward shaders are different programs | `package/Shaders/{DFLight,DistantTree,Water,Effect}.hlsl` |
| Forced | Direct world position in forward consumers | Screen/depth reconstruction uses FO4's depth partition, translated at the boundary with canonical t17 and b4 inverse projection; native depth uses `d <= 0.01` / `mad(d,1.01,-0.01)` (`DFComposite.hlsl`) | `FO4/TerrainShadowsConsumer.hlsli` |
| Forced | Engine-owned render-state lifecycle | Bind t60 and caller s13 at the post-dirty DrawTriShape boundary, including OG/NG/AE. Native SetDirtyStates resubmits s0–s15 and otherwise overwrites the caller sampler (engine-facts Shader slots & bindings, Draw state flush call) | `RenderHooks.cpp`, `ShaderInjection.{h,cpp}`, `TerrainShadows.cpp` |
| Framework | Host activation, settings, UI and shader ownership | Preserve load=false activation, TOML persistence, forwarding-only DearModdingUI, configured ownership and the stock identity gate; unavailable resources publish identity. Rebind t60 after UAV production and restore the per-draw low sampler without widening compute cleanup | `TerrainShadows.cpp`, `Feature.h`, `ShaderInjection.{h,cpp}` |
| Framework | Upstream buffer viewer | Retain fullscreen shadow/heightmap views and sampled field statistics; host b7/t61 supplies debug constants/texture, separate from production b6/t60, without a feature-owned debug CB | `TerrainShadows.cpp`, `FO4/TerrainShadowsConsumer.hlsli`, `ShadowStatistics.cs.hlsl` |
| Fix | Child readiness checks its own editor ID | Check readiness against the resolved inherited-land parent, correcting the upstream loading/readiness mismatch listed under Upstream PR candidates | `TerrainShadows.cpp` `ResolveWorldspaceEditorId`, `EnsureLiveResources` |

## Not supported

| Kind | Upstream | Evidence / boundary | Where |
|---|---|---|---|
| Forced | Skyrim RunGrass shader family | FO4 has no distinct RunGrass target; grass receives the terrain multiplier through the owned deferred directional BSDFLight routes (`ShaderInjectionTargets.h`) | `DFLight.hlsl` |

## Pending

| Kind | Upstream | Remaining work / evidence | Where |
|---|---|---|---|
| Pending | Console/Papyrus GameHour hooks, fast-travel event and completed celestial generation | FO4 wait/sleep/load/interior-exit events request a full refresh, retained hour-jump polling covers large console/script/travel changes, and refresh waits for Sky's consumed hour. Exact small forward-hour edits and active-light versus Sky transition equivalence still need host hooks/evidence; not an accepted parity exception | `TerrainShadows.cpp` `OnDataLoaded`, `PollGameHourJump`, `OnPostDeferredPrePass` |
| Pending | Particle and volumetric sunlight | Native FO4 particles contain only texture × vertex color × ColorScale; upstream reconstructs particle sunlight/ambient. Complete that lighting input boundary rather than shadowing emissive color. No particle source is shipped. Imagespace currently supplies SSLR, not volumetric generation; reconstruct/add that consumer | Native Particle family; `ShaderInjectionTargets.h` |
| Pending | Reflections and other secondary views | BSLighting remains stock: secondary views need their own camera/depth publication. Engine-facts BSLighting forward-pass source limits these passes to modes 0/21; b4/t17 publish the main view | `SharedData.cpp`, `FO4/TerrainShadowsConsumer.hlsli` |
| Pending | Host input/runtime proof | Verify xLODGen orientation/altitude against landscape, active directional light equivalence at transitions, persistent t60 and per-draw s13 for every consumer in an authorized batched runtime session; static shader tests alone are insufficient | `TerrainShadows.cpp`, reconstructed consumers |

