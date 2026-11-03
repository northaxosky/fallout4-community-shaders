# Dynamic Cubemaps

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Authored 1x1 sentinel envmaps, Creator export and the Reflectance target select dynamic-reflection materials | Selection uses all surface water and native envmap materials at the composite probe sites; authored per-material selection is Pending | Native cube-array registration rejects widths below 128 and the G-buffer has no reflectance/F0 channel |
| Forced | Capture before the deferred composite | Capture runs after the Forward cloud group | FO4 draws the sky after the composite |
| Forced | Capture the main color target | Geometry rebuilt as `3 * albedo * (diffuse A + diffuse B) + emissive`; sky from scene color | FO4 has no diffuse-only target |
| Forced | Sky depth reconstructs a finite far-plane position | Sky depth 1.0 is placed on the far-plane direction | FO4's world projection has an infinite far plane |
| Forced | Single world projection | Native world depth partition decoded; first-person pixels excluded | First person occupies the near partition |
| Forced | Inference reads the engine reflection cube, rendered every frame | The host renders the sky into the engine cube (one polar and at most one side face per frame, sky root, previous camera origin); interiors, hidden sky or no fresh cube fall back to scene sky. IBL is a second consumer | Vanilla FO4 never writes the engine cube |
| Divergence | Engine cube holds LOD land, objects and trees plus the sky on every face, refreshed every frame | LOD is drawn on the side and down faces, refreshed a face at a time; the up face is sky only (`engine_cube_lod`, default on) | FO4 object LOD is ~390 draws per face (~1 ms CPU) |
| Forced | LOD land and objects drawn with their forward Lighting shaders | LOD draws with the base forward Lighting technique, an approximation of Skyrim's LOD shading | FO4 ships no forward Lighting permutation for LOD bases, and cube mode 21 draws nothing for FO4 LOD |
| Forced | Water blends the dynamic cube with the engine cube `CubeMapTex` by distance | Same blend; without a valid dynamic or engine cube water keeps its stock sky-gradient reflection | Permutations and sessions can lack a cube |
| Forced | Sentinel materials sample the dynamic cube | Native envmap pixels use the live cube, luminance-normalized to the authored cube; LOD is `(1 - gloss) * 8`; `material_reflections` (default 0.25) blends authored to dynamic, scaled by `1 -` rain | The native composite multiplies this pattern by surface light, and the G-buffer stores gloss, not roughness |
| Divergence | `CreateCubemapRenderTarget_Reflections` and `CreateDepthStencil_Reflections` shrink the engine cube and its depth to 256 | `engine_cube_size` (default 1024) sizes the rendered cube and grows depth 7 to match; consumers read it box-filtered to 256 | FO4 object-LOD trees are sub-texel at 256 and scramble between redraws |
| Divergence | Water reflection weight is fresnel with the authored F0 | F0 is scaled by FO4's reflection amount (`VarAmounts.y`); weight is fresnel alone | FO4 F0 is about 4x Skyrim's; unscaled it mirrors the sky on near water |
| Divergence | `activeReflections` from Skyrim's reflections prepass | Exterior water always uses the reflections variant | Fixed exterior-water policy |
| Divergence | Loaded DC defines `ENABLESSR` for water | Live `enabled_ssr` gates all SSLR while DC is enabled | Avoids recompilation; SSLR feeds surfaces and water |
| Divergence | SSLR confidence is linear | FO4 squares it; linear while DC is loaded | Stock bytes stay identical without DC |
| N/A | Forward `Lighting.hlsl` sentinel path | | FO4 world and first-person emit no forward BSLighting passes |
| Pending | Authored per-material selection and TruePBR/complex material reflectance | Needs an FO4 reflectance/selection contract and authored assets | |
| Pending | Material authoring/export workflow replacing the Creator sentinel DDS export | Needs the selection contract first; 1x1 sentinel cubes cannot select native deferred materials | |

## Upstream bugs

- `CaptureCommon.hlsli` `SampleCapture`: bilinear depth can blend foreground/background or projection partitions into a nonexistent surface.
