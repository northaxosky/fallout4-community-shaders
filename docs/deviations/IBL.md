# Image Based Lighting

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Always on | Defaults off | |
| Divergence | Consumers `#undef` `IBL` without `DYNAMIC_CUBEMAPS` | `Load` fails unless Dynamic Cubemaps is loaded | DistantTree is not a DC target |
| Forced | Sky SH from the reflections cube, rendered every frame | Host `SkyReflectionCube` ([Dynamic Cubemaps](DynamicCubemaps.md)), rendered only while IBL asks; a stale or interior cube clears the sky SH to zero | Vanilla FO4 never writes the engine cube |
| Forced | Lighting replaces `directionalAmbientColor`, then Skylighting scales it | Replaces the vanilla diffuse ambient in DFLight and the tiled pass before the Skylighting scale; reflection-direction ambient keeps only the Skylighting scale | Deferred ambient is a separate linear DALC addend |
| Forced | SSGI recompose uses `Masks.z` luminance | Luminance from a replay of the lighting ambient (ambient times albedo) | FO4 has no `Masks.z` |
| Forced | Specular IBL in `DeferredCompositeCS`: `reflectance * (envSpecular + skySpecular)` | Evaluated at every composite probe site for all `DALCMode` values; radiance terms are divided by the native surface light sum so the native product restores them; mode 2/3 uses that sum as DALC brightness; `ReflectionNormalisationScale` is unused | The native composite multiplies the probe by surface light and has no reflectance channel |
| Forced | Fog tint in Lighting, Water, ISSAOComposite | Tints the vanilla fog color at the DFComposite fog families, Water, Effect and DistantTree, ahead of the sun and grey blends | FO4 adds sun and grey blends per pixel |
| Forced | Distant tree ambient from `GetAmbient(normal)` | Position rebuilt from depth; normal is `-cross(ddx, ddy)`; without IBL the stock flat ambient stays. Distant-tree DALC is not ported | The forward stage has no world position |
| Divergence | Effect lighting runs for every lit effect | IBL alone leaves effects stock apart from the fog tint | Effect lighting is gated separately |
| N/A | `DisableInWorldMap`, `isMapMenuOpen` | | FO4 has no 3D world-map scene |
| N/A | `UseStaticIBL`, `DiffuseIBL.dds`, `SpecIBL.dds` | | Out-of-world draws receive no ambient term in FO4 |
| N/A | Water ambient and underwater refraction fog tint | | FO4 water ambient is a sun lobe, not DALC |
| N/A | `Lighting.hlsl`, `Particle.hlsl`, `RunGrass.hlsl`, `ISSAOComposite.hlsl` consumers | Composite fog families and DFLight ambient cover them | FO4 has no forward BSLighting or BSParticleShader passes |
| Pending | `EFFECTS11` blocks (`GetDiffuseIBL` sky tilt, volumetric rays IBL) | Effects11 integration | |
