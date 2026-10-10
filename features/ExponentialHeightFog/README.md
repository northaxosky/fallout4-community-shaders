# Exponential Height Fog

Uses the unchanged analytic and four-pass volumetric shaders from the shared pin (upstream
`488e408a9`), including the second fog layer, volumetric noise and the near/far volume split.
This replaces the native-ramp extinction approximation.

Set `load = true`, configure the relevant native shader ownership and restart.
`enabled` toggles the effect live; `volumetricFogEnabled` selects the volume pipeline.
Generated defaults contain the upstream settings and ranges. RGBA colors use four-element
TOML arrays. The Fog Factor debug view and telemetry remain available.
Remove legacy `density_multiplier` and `height_falloff_multiplier` overrides; they have
no equivalent in the upstream model. `enabled` is an upstream integer toggle (`0` or `1`).

## Weather profiles

Profiles live in the user configuration's `features.ExponentialHeightFog.weather` map.
Use a plugin-local weather form ID, not its load-order-prefixed runtime ID:

```toml
[features.ExponentialHeightFog.weather."0x123~Example.esp"]
__enabled = true
fogDensity = 0.02
fogHeight = 500.0
fogInscatteringColor = [0.2, 0.3, 0.4, 1.0]
volumetricFogEnabled = 1
```

Restart after editing profiles. Omitted variables fall back to user settings.
Float and RGBA values interpolate through FO4's weather transition; integer toggles
switch above 0.5. Profiles accept only the upstream 30 weather variables, not grid/history
controls. Fog density is independently authored; native near/far ramps are not fitted
into an extinction model.

## Current limits

Directional shadow scattering, analytic cubemap inscattering, IBL, CloudShadows and
clustered local-light providers are pending. Terrain shadow scattering uses the existing
validated heightfield when available, and sky-light scattering uses the Skylighting probe
array when that feature is healthy. Native fog color is supplied
by the reconstructed shader equations, but native weather uploader/fade-brightness and
sky-color mappings are unverified. Map/reflection coverage and native godray coexistence
are also pending. See `docs\deviations\ExponentialHeightFog.md`; this is not a full parity claim.

## Required in-game validation

After separate deployment/DevBench authorization, capture a build-pinned AE frame and verify:

- Native fog with the feature off; analytic-only and volumetric modes; vanilla suppression
  on/off; effect alpha/add/multiply blends, water surface/LOD and distant trees.
- One post-sky application after alpha/additive/mask layers; geometry and inactive
  allocation pixels unchanged; the copied logical MainTemp output reaches the final scene.
- Current camera-owned b4, exact b6 fog block and no fog b7; RGBA16F volumes, R32 depth,
  dispatch groups, t19, t22, terrain t60 and restored OM/high-slot state.
- Consecutive-frame history, TAA off/on, camera-origin changes, load/teleport and
  dynamic-resolution changes; no history or active-region edge artifacts.
- Weather profile interpolation, missing/disabled profiles, live settings, persistence
  and debug selection; interiors and world/first-person projection boundaries.
- Unbound shadow/local-light/IBL providers and native godray overlap are reported honestly.

Use the DevBench-owned game and an authorized disposable fixture; protect named saves
and leave MO2/profile/INI configuration unchanged.
