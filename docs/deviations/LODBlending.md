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
| Forced | Consumers in `Lighting.hlsl` (`LODOBJECTS`/`LODOBJECTSHD`, `LODLANDSCAPE`, `LANDSCAPE`, `LOD_LAND_BLEND`) and `RunGrass.hlsl` | FO4's world frame is deferred, so the consumers live in the reconstructed BSDFPrePass pixel stage: object LOD `LOD_OBJECT_INSTANCED && !LOD_LANDSCAPE`, terrain LOD `LOD_LANDSCAPE && !BONE_TINTING`, near terrain `LANDSCAPE` (+ `LAND_LOD_BLEND`), grass `GRASS` | `package/Shaders/DFPrepass.hlsl` |
| Forced | `input.Color.xyz = 1` before lighting reads vertex color | Overwrites the prepass input vertex color for `LANDSCAPE` and `GRASS` so every later consumer, including the SSGI vertex-AO term, sees neutral color | `package/Shaders/DFPrepass.hlsl` |
| Framework | Host activation, settings, UI and shader ownership | `load = false` activation, TOML persistence, forwarding-only DearModdingUI, configured ownership and the stock identity gate; the define is injected on the deferred prepass only and unavailable delivery publishes neutral values | `LODBlending.cpp`, `FeatureBuffer.cpp`, `SharedData.cpp` |

## Not supported

| Kind | Upstream | Evidence / boundary | Where |
|---|---|---|---|
| Forced | `LODObjectSnowBrightness` / `LODObjectSnowGamma` and the projected-UV snow branch | FO4 BSDFPrePassShader has no `PROJECTED_UV` macro (fallout4-re `harness\shaders\prepass-macro-rules.json`) and the world frame has no BSLightingShader pass (`docs\engine-facts.md` item 36). The settings are omitted from the schema and UI; the fields stay in the shared ABI struct at 1.0 | `LODBlendingSettings.h`, `SharedFeatureData.h` |

## Pending

| Kind | Upstream | Remaining work / evidence | Where |
|---|---|---|---|
| Pending | Object-LOD identification | fallout4-re `bsdfprepass_lod_object_identity` marks the pixel-stage key bit 0x800 as unverified; a live capture must confirm object-LOD draws bind a PS key carrying it | `package/Shaders/DFPrepass.hlsl` |
| Pending | Runtime proof | Verify terrain/object LOD brightness and gamma, the near-terrain LOD blend and vertex-color removal on terrain and grass in game; static compile tests do not prove the consumers execute | `package/Shaders/DFPrepass.hlsl` |
