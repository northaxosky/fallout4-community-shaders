# ScreenSpaceShadows feature

Screen-space shadow sidecar using Sony Bend SSS-style raymarching plus an optional apply pass on deferred lighting output.

## Hook surface

- `RegisterPostDeferredPrePass()` runs the raymarch pass.
- `RegisterPostDeferredLightsImpl()` runs the apply pass when scene application is enabled.

## Runtime assets

- Shaders live in `features/ScreenSpaceShadows/Shaders/` and deploy to `Data\F4SE\Plugins\ScreenSpaceShadows\`.
- Settings live in `Data\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows.toml`.

## Invariants

- `RaymarchCS.hlsl` is compiled with the runtime `SAMPLE_COUNT` define.
- The Bend SSS helper currently expects `Texture2D<unorm float>` depth input in the deployed compile path.
- ENB detection yields scene application unless explicitly forced.
- Constant buffers must stay 16-byte aligned.

## Smoke gates

- `scripts/smoke-apply-sweep.sh`
- `scripts/smoke-apply-diff.sh`

Smoke markers include `.sss_force_apply` and `.sss_extreme`, read during `LoadSettings()`.
