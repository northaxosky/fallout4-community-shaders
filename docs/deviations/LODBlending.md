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
| Forced | `input.Color.xyz = 1` before lighting reads vertex color | Overwrites the prepass input vertex color for `LANDSCAPE` and `GRASS` so every later consumer, including the SSGI vertex-AO term, sees neutral color | `package/Shaders/DFPrepass.hlsl` |
| Forced | Object-LOD gate: compile-time `LODOBJECTS` / `LODOBJECTSHD` technique defines | FO4's prepass gives LOD objects no distinct technique or descriptor bit (no 0x800 `LOD_OBJECT_INSTANCED` key appears in the AE stock identity outside 0x902/0xa03/0xb03/0x02000a03), so the host classifies each draw once when its constants are baked and the shader reads the result from `cb2_pad.x`, a PS cb2 register stock never reads (`the register between the table-addressed scroll delta (`PixelShader::constantTable` byte 0x5F) and material flags (0x60); the plain variant has six cb2 registers and only five table entries, 0x59, 0x5A, 0x5F, 0x60 and 0x6C, so the pad has none and is addressed as 0x5F plus one register, accepted only when 0x60 is exactly two registers after 0x5F). Discriminator: BTO shapes are the non-`kLODLandscape` shaders of geometry under `Main::GetLandLODRoot()` (the `LODRoot` node: `LODRoot > LandAndObjectLOD > Objects > level > obj > BSMultiBoundNode > BSSubIndexTriShape`, sampling `Textures/terrain/<world>/Objects/<world>.Objects.dds`); terrain LOD under the same node has `kLODLandscape`. The engine markers cannot gate: in an AE 1.11.240 session the `kLODObjects` property flag was set on most BTO shapes but not all (one BTO draw carried flags 0x9180000000), and no material feature `kLODObjects`/`kLODObjectsHD` was ever set. The node named `ObjectLODRoot` (`Main::GetObjectLODRoot()`) holds loaded near-cell objects, not object LOD. Command-buffer draws: `BSDFPrePassShader::CreateCommandBuffer` (ID 1285447 / 2318501) is detoured to classify the pass and scope the lane; `BSShader::BuildCommandBuffer` (833764 / 2318870) writes the lane into the PS constants it copies into the record's immutable cb2, only inside that scope so utility-shader records are untouched. Immediate draws (properties without the 0x400000 flag, which `CreateCommandBuffer` rejects) build cb2 in `SetupGeometry`, so the same lane is written when the engine maps the pixel constant group (ID 2317224 on NG/AE; OG inlines the map, so the load fails there and the gate stays neutral). Landscape draws are skipped because that register is `land_material_gate`. The lane is written for every other prepass draw, 0 or 1, since the constants are not cleared. Stock bytecode without `LOD_BLENDING` is unchanged. A failed install fails the feature load, so the gate stays neutral | `LODBlending.cpp`, `ShaderSubclassHooks.cpp`, `package/Shaders/DFPrepass.hlsl` |
| Framework | Host activation, settings, UI and shader ownership | `load = false` activation, TOML persistence, forwarding-only DearModdingUI, configured ownership and the stock identity gate; the define is injected on the deferred prepass only and unavailable delivery publishes neutral values | `LODBlending.cpp`, `FeatureBuffer.cpp`, `SharedData.cpp` |

## Not supported

| Kind | Upstream | Evidence / boundary | Where |
|---|---|---|---|
| Forced | `LODObjectSnowBrightness` / `LODObjectSnowGamma` and the projected-UV snow branch | FO4 BSDFPrePassShader has no `PROJECTED_UV` macro (fallout4-re `harness\shaders\prepass-macro-rules.json`) and the world frame has no BSLightingShader pass (`docs\engine-facts.md` item 36). The settings are omitted from the schema and UI; the fields stay in the shared ABI struct at 1.0 | `LODBlendingSettings.h`, `SharedFeatureData.h` |

## Pending

| Kind | Upstream | Remaining work / evidence | Where |
|---|---|---|---|
| Pending | Runtime proof | Verify terrain/object LOD brightness and gamma, the near-terrain LOD blend and vertex-color removal on terrain and grass in game; static compile tests do not prove the consumers execute. The `feature=LODBlending` telemetry and one `Object-LOD bake` log line report classified command-buffer records and immediate draws (BTO versus total) and the lanes baked or unavailable | `package/Shaders/DFPrepass.hlsl` |
