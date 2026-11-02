# Terrain Variation

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Consumers in `Lighting.hlsl` | Consumers live in the `DFPrepass` pixel stage: near terrain, LOD blend texture, terrain LOD, and landscape-textured meshes (decals and object LOD excluded) | FO4's world frame is deferred |
| Forced | `ComputeStochasticOffsets(TexCoord0.zw)` | Fed the prepass `lodAlbedoUV * 0.5`; the lattice wraps every 4-cell chunk, leaving an engine-inherited seam | FO4 has no `TexCoord0.zw` |
| Forced | Six landscape layers; spec/gloss in the normal alpha | Four layers with a separate per-layer material map sampled with the same offsets | FO4 landscape material layout |
| Forced | Per-draw mesh-variation bit in the feature descriptor | Host classifies each prepass draw; the shader applies `enableMeshSupport` live | The prepass has no feature descriptor |
| Divergence | Landscape texture eligibility: `landscape/<file>` with no subfolder | `landscape/<folder>/...` for `rocks`, `dlc06rocks`, `ground`, `trees`, `dirtcliffs`, excluding `rockriverstones*`; roads, piers, `stonewall*`, grass, plants, glowingseaash, rrtracks, sinkhole excluded | FO4 stores every terrain texture one folder deep; excluded sets tile poorly |
| Forced | Variation on instanced terrain | Not applied | The engine never binds it |
| Pending | Stochastic RMAOS for `TRUE_PBR` landscape and meshes | No TruePBR consumer yet | |
| Pending | Residual seams in admitted textures | Mild cases remain (`rockslab02`, `driedgrass01`, some mud/silt specs, `dirtclifferoded`) | |
