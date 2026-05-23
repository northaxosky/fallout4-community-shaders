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

## Per-weather profiles

Layers per-category overlays over base settings, blended across the engine's `currentWeather`/`lastWeather` transition.

### Schema (TOML)

```toml
[weather]
enable_per_weather_profiles = true

[weather.clear]
exposure = 1.0
bloom_intensity = 0.55

[weather.rain]
exposure = 0.85
bloom_intensity = 0.75
lut_path = "rain_cool"
lut_enable = true
ca_intensity = 0.6

[weather.overrides]
"0x001E5E60" = "overcast"   # form ID -> category, applied before vanilla lookup
```

Categories: `clear`, `overcast`, `fog`, `rain`, `radstorm`, `snow`, `interior`, `unknown`.

### Overlayable keys

Each `[weather.<category>]` table may set any subset of: `exposure`, `lut_enable`, `lut_path`, `lut_strength`, `bloom_enable`, `bloom_threshold`, `bloom_intensity`, `bloom_mip_weights`, `vignette_enable`, `vignette_intensity`, `ca_enable`, `ca_intensity`, `sunsprite_intensity`, `sunsprite_size`, `lens_flare_enable`, `lens_flare_intensity`, `lens_flare_ghosts`, `dirt_enable`, `dirt_intensity`. Unset keys fall through to `[settings]`.

NOT overlayable: `sunsprite_enable` (engine sunbeams vfunc hook reads the persisted `settings.sunspriteEnable` directly; a frame-overlaid value would desync the hook), all `dof_*`/`aperture`/`focus_*` keys (also hook-gated), and all global keys (`enabled`, `preset`, `force_with_enb`, `tonemap_operator`, `adaptive_exposure*`, `exposure_key/min/max`, `bloom_mips`, `sharpen_*`).

### Blend math

Resolver builds two endpoints (`prev = base + overlay[prevCat]`, `cur = base + overlay[curCat]`), then linearly lerps numeric keys across `currentWeatherPct`. Booleans and integers snap at `pct >= 0.5`. LUT paths swap at `pct >= 0.5` via the LUT cache (no per-frame DDS load).

### LUT cache

`LoadSettings` and "Reload weather profiles" call `LUTCache::Preload` for every referenced `lut_path`. The render-thread `Resolve` only consults `LUTCache::TryGet` (no synchronous I/O). On a cache miss, the resolver falls back to the base LUT SRV and surfaces a one-shot warning.

### Sampling and threading

`SampleSky` reads `Sky::currentWeather/lastWeather/currentWeatherPct/mode` on the render thread. Pointer reads are atomic on x64; a one-frame torn read of `currentWeatherPct` is clamped to `[0, 1]`. `mode != kFull` (interior, sky-dome-only) falls back to base.

### Classifier

User overrides (`[weather.overrides]`) take precedence, then a binary search over the 52-entry vanilla weather table seeded from `Fallout4RE/exports/cs-weather-state-machine.json`, then a fallback that reads `TESWeather::weatherData[kFlags]` (kSnow -> snow, kRainy -> rain, kCloudy -> overcast, else clear). FormID overrides are load-order-fragile and should be authored against the leveled set in use.

## Smoke gates

- `scripts/smoke-imagespace-presets-sweep.sh`
- `scripts/smoke-imagespace-sweep.sh`
- `scripts/smoke-imagespace-dof-sweep.sh`
- `scripts/smoke-imagespace-weather.sh` (resolver-only by default; set `SMOKE_WEATHER_ENGINE_MODE=1` with an exterior save to also exercise `Sky::ForceWeather`)

Smoke markers use `.imagespace_force_*` files next to the INI and are read during `LoadSettings()`.
The weather harness uses `.imagespace_force_weather_category` (single digit '0'-'7' matching the
`WeatherCategory` enum order) for resolver-only mode and `.imagespace_force_weather_formid` (hex
string) for engine-integrated mode.

