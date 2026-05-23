# Imagespace feature

Post-upscale compute stack for tone mapping, LUT grading, adaptive exposure, bloom, lens effects, CAS sharpening, and optional Bokeh DOF.

## Hook surface

- `OnPostPostLoad()` wraps the post-upscale imagespace call so it runs after Upscaling's `Load()` hooks.
- DOF engine `IsActive` vfuncs are patched so the engine DOF yields when Imagespace DOF is enabled.

## Runtime assets

- Shaders live in `features/Imagespace/Shaders/` and deploy to `Data\F4SE\Plugins\FO4CommunityShaders\Imagespace\Shaders\`.
- Settings live in `Data\F4SE\Plugins\FO4CommunityShaders\Imagespace.toml`.
- LUT files are loaded from the configured `lut_path` setting.

## Invariants

- ENB is treated as owner of its overlapping effects unless `bForceWithENB=true`.
- Adaptive exposure generates the log-luma pyramid with per-mip SRVs and per-mip UAVs, then reads the full chain only during the exposure pass.
- Composite output alpha stays `1.0`: FO4's `kMain` runtime format is `R11G11B10_FLOAT`, so there is no stored framebuffer alpha to preserve.
- Lens dirt mask is 256x256 R8 procedural noise generated once at first composite dispatch; modulated by sun-glow magnitude and applied in HDR linear domain post-bloom-add, pre-exposure.
- Bokeh DOF keeps near and far half-res blur buffers separate; composite applies far blur over sharp, then near blur on top.
- Constant buffers must stay 16-byte aligned.

## Smoke gates

- `scripts/smoke-imagespace-presets-sweep.sh`
- `scripts/smoke-imagespace-sweep.sh`
- `scripts/smoke-imagespace-dof-sweep.sh`

Smoke markers use `.imagespace_force_*` files next to the INI and are read during `LoadSettings()`.
