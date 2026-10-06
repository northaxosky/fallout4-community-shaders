# LOD Blending

Rules, Kind legend and cross-cutting records: [README](README.md).

Upstream pin: `d330bf12d`; shared pin: `6f81ebc2512da5564f37e728a65037b4c45e2a67`. Settings keys, defaults
and ranges match the pin: `LODTerrainBrightness`, `LODObjectBrightness` (0.01-5), `LODTerrainGamma`,
`LODObjectGamma` (0.1-3) and `DisableTerrainVertexColors`. The GPU block is the existing
`LODBlendingSettings` layout, published raw with no CPU-derived values, and defaults to 1.0 so an
unpublished block is neutral. DistantTree has no upstream consumer.

Feature classification: **core** (rule 2: upstream brightness/gamma and vertex-color algorithm).
Pending rows remain unfinished and do not establish upstream parity.

## Translations

| Kind | Upstream | Fallout 4 / evidence | Where |
|---|---|---|---|
| Forced | Consumers in `Lighting.hlsl` (`LODOBJECTS`/`LODOBJECTSHD`, `LODLANDSCAPE`, `LANDSCAPE`, `LOD_LAND_BLEND`) and `RunGrass.hlsl` | FO4's world frame is deferred, so the consumers live in the reconstructed BSDFPrePass pixel stage: object LOD behind the baked gate below, terrain LOD `LOD_LANDSCAPE && !BONE_TINTING`, near terrain `LANDSCAPE` (+ `LAND_LOD_BLEND`), grass `GRASS` | `package/Shaders/DFPrepass.hlsl` |
| Forced | `input.Color.xyz = 1`, else landscape max-channel normalization, before lighting reads vertex color | Rewrites the prepass input vertex color for `LANDSCAPE` and `GRASS` (normalization on `LANDSCAPE` only, as upstream) so every later consumer, including the SSGI vertex-AO term, sees it | `package/Shaders/DFPrepass.hlsl` |
| Forced | Object-LOD gate: compile-time `LODOBJECTS` / `LODOBJECTSHD` technique defines | FO4's prepass has no LOD technique or descriptor bit (no 0x800 `LOD_OBJECT_INSTANCED` key in the AE stock identity outside 0x902/0xa03/0xb03/0x02000a03), and the engine markers miss BTO: in an AE 1.11.240 session `kLODObjects` was set on most BTO shapes but not all (one carried flags 0x9180000000) and no `kLODObjects`/`kLODObjectsHD` material feature was ever set. The host classifies each draw instead: BTO shapes are the non-`kLODLandscape` shaders of geometry under `Main::GetLandLODRoot()` (`LODRoot > LandAndObjectLOD > Objects > level > obj > BSMultiBoundNode > BSSubIndexTriShape`, sampling `Textures/terrain/<world>/Objects/<world>.Objects.dds`); terrain LOD under the same node has `kLODLandscape`. `Main::GetObjectLODRoot()` holds loaded near-cell objects, not object LOD. The result reaches the shader as `cb2_pad.x` | `LODBlending.cpp` `IsObjectLOD`, `package/Shaders/DFPrepass.hlsl` |
| Forced | Gate carrier | PS cb2 pad lane (`cb2_pad`, declared directly before `cb2_material_flags`, which stock never reads). It is a float4 shared by prepass features: each registers a classifier for one component through `RegisterPrepassDrawClassifier(PrepassLaneComponent, ...)`, and LOD Blending owns `.x`. The host addresses it as the register before the material-flags entry (`PixelShader::constantTable` byte 0x60), because the scroll-delta entry is absent on hair and instanced variants and sits at 0x5F on the plain variant. All 466 non-landscape prepass pixel shaders in the AE 1.11.240 archive carry the 0x60 entry, and it equals the `DFPrepass.hlsl` `cb2_pad` register for every BLEND, SKIN_TINT and GRADIENT_REMAP/HAIR layout. The register is written for every non-landscape prepass draw (registered components 0 or 1, the rest 0) because constants are not cleared; landscape draws are skipped because that register is `land_material_gate`. Stock bytecode without `LOD_BLENDING` is unchanged; a failed install fails the feature load, so the gate stays neutral | `ShaderSubclassHooks.cpp`, `package/Shaders/DFPrepass.hlsl` |
| Forced | Carrier, command-buffer draws | `BSDFPrePassShader::CreateCommandBuffer` (ID 1285447 / 2318501) is detoured to classify the draw and scope the lane; `BSShader::BuildCommandBuffer` (833764 / 2318870) writes the lane into the PS constants it copies into the record's immutable cb2, only inside that scope so utility-shader records are untouched | `ShaderSubclassHooks.cpp` |
| Forced | Carrier, immediate draws | Properties without the 0x400000 flag, which `CreateCommandBuffer` rejects, build cb2 in `SetupGeometry`, so the lane is written into the mapped level-2 pixel constants of the scoped call. NG/AE (ID 2317224) inline the unmap but keep the map out of line: written when the engine maps the pixel group. OG (1.10.163) inlines the map but keeps `BSGraphics::Renderer::FlushConstantGroup` (ID 1515598, RVA 0x2801E40) out of line, called once from `SetupGeometry` to unmap both level-2 groups: written there, after the engine fills the constants and before the unmap | `ShaderSubclassHooks.cpp` |
| Framework | Host activation, settings, UI and shader ownership | `load = false` activation, TOML persistence, forwarding-only DearModdingUI, configured ownership and the stock identity gate; the define is injected on the deferred prepass only and unavailable delivery publishes neutral values | `LODBlending.cpp`, `FeatureBuffer.cpp`, `SharedData.cpp` |

## Not supported

| Kind | Upstream | Evidence / boundary | Where |
|---|---|---|---|
| Forced | `LODObjectSnowBrightness` / `LODObjectSnowGamma` and the projected-UV snow branch | FO4 BSDFPrePassShader has no `PROJECTED_UV` macro (fallout4-re `harness\shaders\prepass-macro-rules.json`) and the world frame has no BSLightingShader pass (`docs\engine-facts.md` item 36). The settings are omitted from the schema and UI; the fields stay in the shared ABI struct at 1.0 | `LODBlendingSettings.h`, `SharedFeatureData.h` |

## Pending

| Kind | Upstream | Remaining work / evidence | Where |
|---|---|---|---|
| Pending | Runtime proof on OG/NG | AE 1.11.240 is proven in game: `lanes_unavailable=0`, `lod_records>0`, and the skyline brightened at `LODObjectBrightness` 3.0. OG and NG were verified only from the address databases; run the in-game check on each. Near-terrain `LAND_LOD_BLEND` and the terrain/grass vertex-color removal still need in-game confirmation. `feature=LODBlending` telemetry reports classified records and immediate draws (BTO versus total) and the lanes baked or unavailable | `package/Shaders/DFPrepass.hlsl`, `ShaderSubclassHooks.cpp` |
