# Screen Space GI

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Compose in `DeferredCompositeCS` | Diffuse is composed in the native 2D accumulator, 2D fog and cube IBL families; cube specular samples the same SH/HQ data before native material weighting | FO4 composes separate diffuse/specular accumulators and the cube equation applies `gloss * material.z^2 * 50` |
| Forced | Current shaded diffuse radiance from the main color target | Rebuilt as `3 * albedo * (diffuse A + diffuse B) + emissive` | Native targets hold unshaded linear lighting |
| Forced | Octahedral G-buffer normal and glossiness | Native spherical view normal re-encoded; glossiness feeds Z only with `GI_SPECULAR` | `DFPrepass` uses spherical encoding |
| Forced | `Color::Ambient(GetAmbient(N))` with `Masks.z` | Powered DALC times albedo, clamped to native diffuse; scaled by Skylighting when present; carries the [IBL](IBL.md) replacement when enabled | FO4 folds the 2.2-power DALC into diffuse and has no `Masks.z` |
| Forced | Skyrim SSAO generation toggle | Native SAO_CS active/applied bits are controlled per frame | Native DrawModel and the console rewrite them |
| Forced | `Masks2.x` vertex AO | `1 - vertexAO` in emissive logical 31.a, shared with Skylighting; blended hair, landscape, skin tint and eye permutations write 0, so vertex AO is 1 there | No separate vertex-AO channel exists |
| Forced | History persists across render-size changes | History resets when the working size changes | FO4 dynamic resolution keeps targets at allocation size |
| Divergence | Fog gray is the shaded main color | Unblended 2D fog (no `COMPOSITE_SCENE_BLEND`) skips `ComposeDiffuse`; its desaturation luminance is the native diffuse | Output is fog color and alpha, so composing only shifted its gray term |
| Divergence | AOPower default 1, range 0-6 | Default 4, range 0-12 | Calibrated for interiors dominated by placed lights |
| Pending | Specular consumers beyond native cube SH/HQ | Other specular provider families need adapters | |

## Upstream bugs

- `gi.cs.hlsl`: the experimental specular half-angle calculation uses inconsistent angular units.
- `Deferred.cpp`: experimental HQ specular binds null diffuse Y/CoCg SRVs although `DeferredCompositeCS.hlsl` still samples them.
