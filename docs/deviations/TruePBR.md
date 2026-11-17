# TruePBR

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Mesh material data comes from a NIF shader type and per-NIF overrides | BGSM version 100: a vanilla v2 material (Part 1) followed by CS blocks (Part 2) | FO4 BGSMs override NIF shader data |
| Divergence | Roughness, ambient occlusion and F0 are material inputs of the lighting pass | Carried through the deferred G-buffer: roughness and occlusion are 8-bit, F0 is 11/11/10-bit float | FO4 lights deferred |
| Divergence | Wet surfaces gain a water lobe | Native wetness smooths and darkens them | Wetness Effects is not ported |
| Divergence | Screen-space occlusion applies to the lit color | Without SSGI, native SSAO occludes only the ambient diffuse and the environment of a PBR pixel | Direct light and emission are not ambient |
| Divergence | Each pixel shades one surface | Blended edges between PBR and non-PBR surfaces mix parameters, and the class follows 50% coverage | Deferred G-buffer |
| Divergence | No-specular and attenuation-only lights are separate permutations | Tiled lights map attenuation-only and no-specular flags as the raster families do | One tiled shader serves every light |
| N/A | Point-light parallax soft shadows on PBR surfaces | Not applied | See Extended Materials |
| Pending | Interlayer parallax of two-layer materials | Those records render flat | Needs the two-layer lobe |
| Pending | Terrain Variation heights and stochastic sampling on PBR surfaces | PBR surfaces march the plain height map | PBR pixels skip the mesh variation lane |
| Pending | Texture sets use editor-ID JSON | Not read | Not ported yet |
| Pending | Vertex color interpolates encoded | Decoded per pixel from the linear value the vertex shader outputs | The vertex shader gamma-decodes before interpolation |
| Pending | Parallax on model-space-normal variants | PBR materials there render flat | Those variants compile without the marcher |
| Pending | Skin, face and hair materials | A vanilla part flagged skin tint, facegen or hair renders as Part 1 | Actor tint pipelines are separate |
