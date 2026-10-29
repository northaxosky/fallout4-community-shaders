# Subsurface Scattering

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Per-pixel amount and Base/Human profile in the masks; runs only after a skin draw | Native class-5 pixels outside first person get full amount and the Human profile; Base settings stay but go unused; the pass runs every frame and skips other pixels | FO4 has no per-pixel amount or profile channel and no skin-draw signal |
| Forced | First-person meshes are drawn after the pass and never scattered | First-person skin keeps the stock ring blur and its first-person depth scale; the pass skips those pixels | FO4 draws first-person in the deferred pass with its own projection, so it would otherwise be scattered at the wrong scale |
| Pending | Per-pixel amount and profile selection | Needs the per-pixel profile extension | |
| Forced | Octahedral normals and opaque albedo alpha | Native spherical normals are re-encoded and albedo alpha is 1 | FO4's alpha is a material scalar |
| Forced | Blur runs before SSGI in `DeferredCompositeCS` | SSS also blurs SSGI's diffuse on skin | FO4 composes SSGI in the first composite |
| Pending | SSGI composed after SSS | Needs SSGI split from the first composite | |
| Divergence | No native skin blur | Replaces the native second-composite skin blur outside first person; it runs again when the feature is disabled or did not run that frame | Disabled must be stock |
| Forced | No native skin blur parameters | FO4's `[SSSSS]` INI parameters are ignored outside first person while enabled | The native blur they tune is replaced there |
| Forced | Scales `characterLightParams[2]` | Scales the rim and fill lanes | FO4's character light reads only those lanes |
| Divergence | Character lighting off by default | Enabling the feature turns FO4's default-on character light off until `EnableCharacterLighting` is set | Upstream default kept |
| Forced | Typed UAV load on the main color target | The feature stays inert on GPUs without typed UAV loads for R11G11B10 | Burley and the vertical pass read the target in place |
| Divergence | Nested `BaseProfile` and `HumanProfile` settings | Profile settings are flat TOML keys such as `BaseProfileBlurRadius` | Settings schemas are flat scalars |
