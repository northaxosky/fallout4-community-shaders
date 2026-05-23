# Imagespace feature

Post-upscale compute stack for tone mapping, LUT grading, adaptive exposure, bloom, lens effects, CAS sharpening, and optional Bokeh DOF.

Layered model:

1. **Base settings** (`Imagespace.toml` `[settings]` table).
2. **Per-weather overlays** (`[weather.<category>]` subtables) - blended across the engine's `currentWeather`/`lastWeather` transition.
3. **Style** quick-pick (Custom / Subtle / Standard / Vivid / Cinematic) - hardcoded subset recipes for tonemap + bloom + lens; edits to tracked sliders switch back to Custom.
4. **Preset** file library - whole-snapshot `[settings] + [weather]` TOML files under `Imagespace/Presets/`. Builtin presets ship under `Imagespace/Presets/Builtin/`; user presets sit in the parent directory.

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

NOT overlayable: `sunsprite_enable` (engine sunbeams vfunc hook reads the persisted `settings.sunspriteEnable` directly; a frame-overlaid value would desync the hook), all `dof_*`/`aperture`/`focus_*` keys (also hook-gated), and all global keys (`enabled`, `style`, `force_with_enb`, `tonemap_operator`, `adaptive_exposure*`, `exposure_key/min/max`, `bloom_mips`, `sharpen_*`).

### Blend math

Resolver builds two endpoints (`prev = base + overlay[prevCat]`, `cur = base + overlay[curCat]`), then linearly lerps numeric keys across `currentWeatherPct`. Booleans and integers snap at `pct >= 0.5`. LUT paths swap at `pct >= 0.5` via the LUT cache (no per-frame DDS load).

### LUT cache

`LoadSettings` and "Reload weather profiles" call `LUTCache::Preload` for every referenced `lut_path`. The render-thread `Resolve` only consults `LUTCache::TryGet` (no synchronous I/O). On a cache miss, the resolver falls back to the base LUT SRV and surfaces a one-shot warning.

### Sampling and threading

`SampleSky` reads `Sky::currentWeather/lastWeather/currentWeatherPct/mode` on the render thread. Pointer reads are atomic on x64; a one-frame torn read of `currentWeatherPct` is clamped to `[0, 1]`. `mode != kFull` (interior, sky-dome-only) falls back to base.

### Classifier

User overrides (`[weather.overrides]`) take precedence, then a binary search over the 52-entry vanilla weather table seeded from `Fallout4RE/exports/cs-weather-state-machine.json`, then a fallback that reads `TESWeather::weatherData[kFlags]` (kSnow -> snow, kRainy -> rain, kCloudy -> overcast, else clear). FormID overrides are load-order-fragile and should be authored against the leveled set in use.

## Smoke gates

- `scripts/smoke-imagespace-style-sweep.sh`
- `scripts/smoke-imagespace-sweep.sh`
- `scripts/smoke-imagespace-dof-sweep.sh`
- `scripts/smoke-imagespace-weather.sh` (resolver-only by default; set `SMOKE_WEATHER_ENGINE_MODE=1` with an exterior save to also exercise `Sky::ForceWeather`)
- `scripts/smoke-imagespace-preset-load.sh`

Smoke markers use `.imagespace_force_*` files next to the INI and are read during `LoadSettings()`.
The weather harness uses `.imagespace_force_weather_category` (single digit '0'-'7' matching the
`WeatherCategory` enum order) for resolver-only mode and `.imagespace_force_weather_formid` (hex
string) for engine-integrated mode.
The preset harness uses `.imagespace_force_preset` (single-line text payload; either a `<scope>:<name>`
identity like `B:Default` / `U:MyLook`, or a bare display name resolved via `FindByName(preferUser=true)`).
The marker always wins over `[preset]` auto-load.

## Presets (file library)

A **preset** is a TOML file containing a full `[settings]` + `[weather]` snapshot. Loading a preset replaces in-memory settings and weather profiles wholesale; the `[preset]` block in `Imagespace.toml` tracks the active identity for auto-restore on next boot.

### Directory layout

```
Data\F4SE\Plugins\FO4CommunityShaders\Imagespace\
    Imagespace.toml                          base config; tracks [preset].active
    Presets\
        Builtin\                             shipped, read-only by convention
            Default.toml
            Neutral-Realistic.toml
            Cinematic-Night.toml
            Vivid-Daylight.toml
            Reactor-Inspired.toml
        <user-name>.toml                     user-authored, writable
    LUTs\
        Reactor-Warm.dds                     shipped LUT, referenced by Reactor-Inspired
        <user-lut>.dds
```

### Identity

Presets are keyed by a stable `identity` string: `"B:"` + lowercase name for builtin, `"U:"` + lowercase name for user. Display names preserve original case. Lookups are case-insensitive. The `[preset]` block persists the lowercase identity; the in-memory `activePresetName` is informational only.

### File format

```toml
[meta]
name = "Reactor-Inspired"
created_by = "FO4 Community Shaders"
description = "Warm, slightly desaturated midtone with gentle bloom"

[settings]
tonemap_operator = 1
exposure = 1.05
bloom_intensity = 0.05
# ...all settings keys, identical to Imagespace.toml [settings]

[weather]
enable_per_weather_profiles = true

[weather.clear]
lut_strength = 1.0
bloom_intensity = 0.055

# User presets MAY include [weather.overrides]; builtin presets MUST NOT
# (the loader drops [weather.overrides] from builtin files with a warning).
[weather.overrides]
"0x001E5E60" = "overcast"
```

### Loader semantics

- Unknown tables and keys are silently ignored (forward-compat).
- Missing keys land on `Settings{}` / `WeatherProfiles{}` defaults (not on whatever was previously in memory).
- Builtin presets that contain `[weather.overrides]` drop the table and log a warning. Builtin presets must not stamp user formID mappings; the live `userOverrides` map is preserved across builtin preset loads.
- User presets honor `[weather.overrides]` from the file (so export/import round-trips).
- Parse failure: live state untouched, `lastPresetError` set, no commit.

### Auto-load and marker

`Imagespace.toml` carries:

```toml
[preset]
active = "b:reactor-inspired"
auto_load_on_boot = true
```

On `LoadSettings()`:

1. Parse `[settings]` + `[weather]` into defaults.
2. Read `[preset]` block.
3. `PresetManager::Refresh()`.
4. If `.imagespace_force_preset` marker is present and resolves: apply that preset (marker wins over auto-load).
5. Else if `auto_load_on_boot && active != ""`: apply by identity (fallback to FindByName), warn if missing.

LUT preload is deferred to `OnD3D11Ready` for the boot path (D3D isn't live during plugin Load); mid-game preset loads call `ApplyLUTState` synchronously.

### UI controls (Presets header)

- Combo: lists all presets with `B:` / `U:` prefix; selection bound to `pendingComboIdentity`.
- **Load**: flushes any pending edits to `Imagespace.toml` (clears local `dirty` flag), then applies the selected preset; marks dirty again so the new `[preset].active` persists on the next save.
- **Save**: disabled for builtin or no active. Writes back to the active preset's path, refreshes, re-anchors active by name.
- **Save As**: opens modal; validates name (`[A-Za-z0-9_-]{1,64}`, no Windows reserved, no "Builtin", no case-insensitive collision); writes to `Imagespace/Presets/<name>.toml`; refreshes; sets active.
- **Delete**: disabled for builtin or no active. Confirm modal. On confirm: deletes the file, refreshes, clears active identity/name/pending, clears `auto_load_on_boot`, persists `Imagespace.toml` immediately.
- **Refresh**: re-scans both directories; re-anchors `pendingComboIdentity` if still present.
- **Auto-load this preset on boot** checkbox - persists into `[preset].auto_load_on_boot`.

### Save As validation

Rejects:

- Empty or > 64 chars.
- Characters outside `[A-Za-z0-9_-]`.
- Windows reserved device names: `CON`, `PRN`, `AUX`, `NUL`, `COM1`..`COM9`, `LPT1`..`LPT9`, `CLOCK$` (case-insensitive).
- The literal `"Builtin"` (case-insensitive).
- Any case-insensitive collision with an existing preset name (builtin or user).

`PresetManager::Save` re-checks file existence right before write (TOCTOU guard between validate and write).


