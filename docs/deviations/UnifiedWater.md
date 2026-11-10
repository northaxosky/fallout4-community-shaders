# Unified Water

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | `distanceBlendFactor = 1` drives normals, SSR and specular fractions; fog moves to the pixel shader | Override covers detail normals and the surface color blend; no-DEPTH alpha keeps the raw fade | FO4 SSLR has no distance term and fog is already per pixel |
| Forced | Tile UVs from `ObjectUV` and `CellTexCoordOffset` | Tiles keep the native world-XY UV path | FO4 `ObjectUV` is a flag |
| Forced | `WaterMesh.nif` (stream 100) | Converted to stream 130 with identical geometry | FO4 vertex stream format |
| Forced | Tiles registered with `AddWater` | Registered through `AddLODWater`, with the cell's WATR and height | Bare `AddWater` tiles produce no draw calls |
| Forced | Per-child `SetAppCulled` for loaded cells | Segmented siblings keep native masking; tiles are culled per cell and while no exterior is loaded | FO4 masks loaded cells by segment and dereferences segment data unchecked; it ignores the root cull bit for registered LOD water |
| Forced | Interior hide relies on cull bits | Tiles are unregistered and re-registered through `BGSTerrainChunk::AttachWater` | Interiors without Has-Water never run the water update that refreshes cull bits |
| Forced | One shape per square | Squares of one form and height in a LOD8, LOD16 or LOD32 chunk share one shape with the same quads | Every FO4 tile is its own water draw in every water pass; LOD4 stays per cell for loaded-cell hiding |
| Forced | Cache workers read plugin records | Records are read sequentially at DataLoaded, only when regenerating; one walk per plugin indexes its CELL offsets | FO4 file cursors are unsynchronized; `FindCellInFile` rewalks groups per cell |
| Forced | Sky-cell cleanup and map-menu exception | Exterior state cleared in `ReleaseLowResMaxHeightData` | The Pip-Boy map renders no exterior LOD |
| Forced | Material identity folds noise texture names into the CRC | Done at FO4's `TESWaterForm::UpdateWaterMaterial` over three noise textures | FO4 material fill path differs |
| Forced | Cubemap reflection flag on tiles | Tiles keep native reflection flags | FO4 water has no cubemap technique |
| N/A | World flowmap generation and flowmap normals | | FO4 has no flowmap path |
| Forced | LLF lights in the water surface pass | Tiles share the replaced BSWater PS; see [Water Effects](WaterEffects.md) and [Light Limit Fix](LightLimitFix.md) | FO4 water draws local lights in a separate specular technique |
| N/A | Tamriel precache | | Skyrim-only data |
