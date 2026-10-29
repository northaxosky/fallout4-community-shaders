# Subsurface Scattering

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Per-pixel amount and Base/Human profile in the masks; runs only after a skin draw | Native class-5 pixels get full amount and the Human profile; Base settings stay but go unused; the pass runs every frame and skips other pixels | FO4 has no per-pixel amount or profile channel and no skin-draw signal |
| Pending | Per-pixel amount and profile selection | Needs the per-pixel profile extension | |
| Forced | Octahedral normals and opaque albedo alpha | Native spherical normals are re-encoded and albedo alpha is 1 | FO4's alpha is a material scalar |
| Forced | Blur runs before SSGI in `DeferredCompositeCS` | SSS also blurs SSGI's diffuse on skin | FO4 composes SSGI in the first composite |
| Pending | SSGI composed after SSS | Needs SSGI split from the first composite | |
| Divergence | No native skin blur | Replaces the native second-composite skin blur; it runs again when the feature is disabled or did not run that frame | Disabling restores stock behavior |
| Forced | No native skin blur parameters | FO4's `[SSSSS]` INI parameters are ignored while enabled | The native blur they tune is replaced |
| Forced | Scales `characterLightParams[2]` | Scales the rim and fill lanes | FO4's character light reads only those lanes |
| Divergence | Character lighting off by default | Enabling the feature turns FO4's default-on character light off until `EnableCharacterLighting` is set | Upstream default kept |
| Forced | Typed UAV load on the main color target | The feature stays inert on GPUs without typed UAV loads for R11G11B10 | Burley and the vertical pass read the target in place |
| Forced | Nested `BaseProfile` and `HumanProfile` settings | Profile settings are flat TOML keys such as `BaseProfileBlurRadius` | Spec-agreed flat settings layout |
