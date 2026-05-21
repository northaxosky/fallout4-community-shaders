# ScreenSpaceGI feature

Phase 1 screen-space AO sidecar for deferred-rendering experiments. It currently builds a linear-depth pyramid, computes half-resolution AO, and optionally applies it after deferred lighting.

## Hook surface

- `RegisterPostDeferredPrePass()` runs `DrawAO()`.
- `RegisterPostDeferredLightsImpl()` runs `Apply()` when scene application is enabled.

## Runtime assets

- Shaders live in `features/ScreenSpaceGI/Shaders/` and deploy to `Data\F4SE\Plugins\ScreenSpaceGI\`.
- Settings live in `Data\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceGI.ini`.

## Invariants

- Pyramid build mip 0 reads scene NDC depth. Later pyramid mips read the previous mip through a one-mip SRV while writing a disjoint UAV.
- AO sampling reads the full depth-pyramid SRV so shader mip selection remains valid.
- Projection reconstruction still uses the historical fallback until a reliable FO4 projection source is exposed.
- Constant buffers must stay 16-byte aligned.

## Smoke gates

- `scripts/smoke-ssgi-sweep.sh`

Smoke markers include `.ssgi_force_apply` and `.ssgi_extreme`, read during `LoadSettings()`.
