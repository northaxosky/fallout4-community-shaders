# WaterEffects

Water parallax reads height from the water normal alpha; vanilla alpha-less normals stay flat.

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Skyrim cell lookup and water form | Loaded `GridCellArray` cells fill an eye-centered 5x5, 4096-unit grid; heights and averaged shallow/deep color times the sky water multiplier come from the cell | FO4 has no reference/player-height query |
| Forced | RGB caustics multiply directional light color in `Lighting.hlsl` | Applied to direct diffuse/specular in all directional BSDFLight families; ambient and local light unaffected | FO4 accumulates direct light separately in RGB targets |
| Divergence | `WaterSystemHeight` absent | Camera cell's resolved plane relative to b4, or -FLT_MAX; not a water-mesh intersection | Simplified data source |
| Forced | `NormalsScale` read in the water pixel shader | The vertex shader forwards it as `TEXCOORD8`; BSWater VS and PS are replaced as a pair | The FO4 water PS has no `NormalsScale` constant |
| Forced | Interior cell water height | Stays -FLT_MAX | FO4's exterior-height accessor rejects interior cells |
| N/A | WaterParallax FLOWMAP variants | | FO4 water has no flowmap path |
| Forced | LLF lights in the water surface pass | Clustered lights replace FO4's SPECULAR-technique lights in world draws; see [Light Limit Fix](LightLimitFix.md) | FO4 water draws its local lights in a separate specular technique |
| Pending | Secondary mode-0/21 BSLighting caustics | Not ported | Needs secondary-view BSLighting execution and camera/resource lifetime |

## Upstream bugs

- `SharedData.hlsli` `GetWaterData`: assumes coordinates are positive after adding 64 cells; `modf` returns a negative fraction west/south of -262144 units, and `round` can pick adjacent tiles at exact cell borders. Use `floor(absolutePosition / 4096) - floor(CameraPosAdjust.xy / 4096) + 2`.
