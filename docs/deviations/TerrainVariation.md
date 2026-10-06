# Terrain Variation

Rules, Kind legend and cross-cutting records: [README](README.md).

Upstream pin: `d330bf12d`; shared pin: `d456e9c5ed5be4e4d3321de5e79669942a93b3fe`. The shared pin
carries `TerrainVariation.hlsli` byte-identical to `d330bf12d` and FO4 stages it unchanged through
`xmake\shared.lua`. Settings keys, defaults and tooltips match the pin: `enableLODTerrainTilingFix`
and `enableMeshSupport`, both on. Upstream has no master toggle and no resources; the GPU block is the
existing `TerrainVariationSettings` layout, published raw, and an unpublished block is zero (LOD fix
and mesh variation off).

Feature classification: **core** (rule 2: upstream stochastic sampling algorithm and consumers).
Pending rows remain unfinished and do not establish upstream parity.

## Translations

| Kind | Upstream | Fallout 4 / evidence | Where |
|---|---|---|---|
| Forced | Consumers in `Lighting.hlsl` (`LANDSCAPE`, `LOD_LAND_BLEND`, `LOD_BLENDING` + `LODLANDSCAPE`, landscape-textured meshes) | FO4's world frame is deferred, so the consumers live in the reconstructed BSDFPrePass pixel stage: near terrain `LANDSCAPE`, the LOD blend texture `LANDSCAPE && LAND_LOD_BLEND`, terrain LOD `LOD_BLENDING && LOD_LANDSCAPE && !BONE_TINTING`, meshes `TEXTURE && !LANDSCAPE` outside the Skyrim exclusions (`LOD_LANDSCAPE`, `SKIN_TINT`, `HAIR`, `EYE`, `TREE_ANIM`; meatcuff variants sample separately). Object LOD has no technique define, so the host rejects BTO shapes with the `IsObjectLODShape` test LOD Blending also uses (`src/Render/ObjectLOD.cpp`); decals are rejected by `kDecal`/`kDynamicDecal`. `TERRAIN_VARIATION` is injected on the deferred prepass only. The terrain-LOD result is the raw sample: the prepass stores raw albedo, so Skyrim's `Color::Diffuse` has no counterpart (same as LOD Blending's gamma step) | `package/Shaders/DFPrepass.hlsl` |
| Forced | `ComputeStochasticOffsets(TexCoord0.zw)` | FO4 has no `TexCoord0.zw`. The equivalent is the prepass `lodAlbedoUV` = layerUV/48 + chunk offset, where Skyrim's zw = layerUV/96 + offset, so the lattice is fed `lodAlbedoUV * 0.5` and `WORLD_SCALE` 332.54 keeps its tile-relative lattice (triangle height 0.25 tile) with the header unchanged. Evidence: `TESObjectLAND::LoadedLandData::InitSDM` (OG 0x3A7190 / NG 0x469620 / AE 0x4BD770) and `InitDistantTextureBlending` (OG 0x3A63A0 / NG 0x4687F0 / AE 0x4BC940): layer UV step `fLandTextureTilingMult`/4 (default 1.5, 0.375 per vertex, 12 per cell) and terrainOffset = ((cell - chunkMin) + {0, 0.5}) / 4, wrapped. The wrap leaves a seam every 4-cell chunk (16384 units) where `lodAlbedoUV` restarts; it is inherited from the engine's coordinate, not introduced here | `package/Shaders/DFPrepass.hlsl` |
| Forced | Six landscape layers; spec/gloss in the normal alpha | FO4 landscape has four layers, and the per-layer material map (`g_tLandMaterial`, spec and gloss) is a separate texture. It is sampled with the same shared offsets as albedo and normal so every datum stays coherent. Mesh variation does the same with `g_tMaterial` | `package/Shaders/DFPrepass.hlsl` |
| Forced | Per-draw `TVMeshVariation` bit in the permutation `ExtraFeatureDescriptor`, set by a `BSLightingShader::SetupGeometry` hook | The prepass has no feature descriptor. The host classifies each prepass draw and the answer rides `cb2_pad.y`, component `.y` of the same pad lane LOD Blending uses for `.x` (see [LOD Blending](LODBlending.md); `RegisterPrepassDrawClassifier(PrepassLaneComponent::kY, ...)` in `Load`). AE 1.11.240: the pad register is unread in all four components across 466/466 non-landscape prepass pixel shaders; landscape draws are skipped because that register is `land_material_gate`. Every lane is written for every non-landscape draw on the command-buffer path (baked at record creation) and the immediate path (every frame). The host bakes eligibility only; the shader applies `enableMeshSupport`, so records baked earlier follow live toggles | `TerrainVariation.cpp`, `ShaderSubclassHooks.cpp`, `package/Shaders/DFPrepass.hlsl` |
| Forced | Alpha test via `NiAlphaProperty` on the geometry, clamp mode and base texture from the shader property | Typed CommonLibF4 members: `BSGeometry::properties[0]` as `NiAlphaProperty` flag 0x200, `BSLightingShaderMaterialBase::textureClampMode` equal to `kWrap_S_Wrap_T`, `BSShaderProperty::GetBaseTexture()` and its `NiTexture::name`. The class gates (`BSLightingShaderProperty`, no `kMultiTextureLandscape`/`kLODLandscape`/`kDecal`/`kDynamicDecal`) are unchanged. The classification cache is keyed by the interned name pointer under a `shared_mutex`, as upstream | `TerrainVariation.cpp` |
| Tweak | Landscape texture means `landscape/<file>` with no further subfolder (upstream #2717) | FO4 vanilla stores every terrain texture one folder below `landscape/` (`Ground/`, `Roads/`, `DirtCliffs/`, `Rocks/`); only 17 of 824 files sit directly in it, mostly decals, so the no-subfolder rule would match about 7 vanilla materials. The prefix is accepted at any depth with every other gate unchanged: 160 eligible vanilla BGSMs and about 53k NIFs | `TerrainVariation.cpp` |
| Tweak | `DataLoaded` collects `landscapeDiffusePaths` from every land texture record | The set is filled but never read by the pinned classifier, so it is not ported, and the mutex comment drops it from its guard list | `TerrainVariation.cpp` |
| Tweak | Non-variation sampling uses `SampleBias(..., MipBias)` | Stock FO4 prepass uses plain `Sample`, so draws without variation keep it. Variation samples (`SampleLevel` at an explicit mip, `SampleBias` for the disabled LOD blend fallback) port unchanged | `package/Shaders/DFPrepass.hlsl` |
| Framework | Host activation, settings and UI | `load = false` activation, TOML persistence, forwarding-only DearModdingUI, configured ownership and the stock identity gate. The settings note keeps upstream's text with "Disable at Boot" replaced by Advanced > Load on startup; the feature is listed under Lighting like LOD Blending. Unavailable delivery publishes zeros | `TerrainVariation.cpp`, `FeatureBuffer.cpp`, `SharedData.cpp` |

## Not supported

| Kind | Upstream | Evidence / boundary | Where |
|---|---|---|---|
| Forced | Variation on instanced terrain (`INSTANCED && LANDSCAPE`, `Texture2DArray` layers) | The engine never binds it (SetupGeometry binder census on OG/NG/AE), and `BSDFPrePassShader::CreateCommandBuffer` rejects technique 0x20. The permutation still compiles stock; the consumers are guarded with `!INSTANCED` | `package/Shaders/DFPrepass.hlsl` |

## Pending

| Kind | Upstream | Remaining work / evidence | Where |
|---|---|---|---|
| Pending | `StochasticEffectParallax` and `StochasticHeightChannel` consumers (terrain and mesh POM, parallax soft shadows) | They need the Extended Materials marcher, which is not ported. The header functions are staged unchanged | Extended Materials slice |
| Pending | Stochastic RMAOS for `TRUE_PBR` landscape and meshes | FO4 has no TruePBR consumer yet. The FO4 material map is already sampled with the shared offsets | TruePBR slice |
| Pending | Runtime proof | In game on AE: `NiTexture::name` prefix form (`landscape/` after `data/` and `textures/` stripping); BGSM decal-to-property-flag mapping; the 9 `EYE \| COMBINED` variants' constant-table 0x60 register equals the pad register; menus with HDR (the #2717 regression class); near terrain, LOD blend texture, terrain LOD and mesh variation visible; settings toggles live; OG/NG verified only from the address databases. `feature=TerrainVariation` telemetry reports classified records, mesh-flagged records, and the lanes baked or unavailable | `package/Shaders/DFPrepass.hlsl`, `ShaderSubclassHooks.cpp` |

## Upstream bug

`Lighting.hlsl` declares `sharedOffset` only under `EMAT`, so `LANDSCAPE` with `TERRAIN_VARIATION` and
without `EMAT` does not compile. The FO4 consumers declare their own offsets, so it does not apply. It is
not patched locally; the fork PR is tracked separately.
