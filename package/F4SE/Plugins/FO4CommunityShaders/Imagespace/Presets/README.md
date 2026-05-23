# Imagespace presets

Drop your own `.toml` files in this directory to make them appear in the Imagespace ImGui "Presets" combo. Files under `Builtin/` ship with the plugin; do not edit those - they get overwritten on update.

## Quick start

1. Open the in-game menu (END key), expand Imagespace > Presets, click "Save As..." and pick a name.
2. The plugin writes the current `[settings] + [weather]` snapshot to `Imagespace/Presets/<name>.toml`.
3. Edit the file by hand to refine; hit "Refresh" + "Load" to reapply.

## File format

```toml
[meta]
name = "My Look"               # display name (informational; filename stem is authoritative)
created_by = "Your Name"
description = "What you were going for"

[settings]
tonemap_operator = 1            # 0=passthrough 1=Hable 2=Reinhard 3=Lottes
exposure         = 1.0
bloom_intensity  = 0.05
# ... any subset of [settings] keys from Imagespace.toml
# Missing keys land on built-in defaults, NOT on whatever the user previously had.

[weather]
enable_per_weather_profiles = true

[weather.clear]
lut_path     = "Reactor-Warm"   # filename stem; resolved under Imagespace/LUTs/
lut_strength = 1.0
bloom_intensity = 0.055

[weather.rain]
lut_strength = 0.6
vignette_intensity = 0.3

# Optional in user presets; IGNORED with a warning in builtin presets.
[weather.overrides]
"0x001E5E60" = "overcast"
```

See `features/Imagespace/SPEC.md` (in the source tree) for the full list of overlayable keys per category, blend math, and which keys are NOT overlayable (e.g. all `dof_*`, all global keys).

## Naming rules (for Save As)

- `[A-Za-z0-9_-]`, 1-64 chars.
- Not a Windows reserved device name (`CON`, `PRN`, `AUX`, `NUL`, `COM1..9`, `LPT1..9`, `CLOCK$`).
- Not the literal `Builtin` (reserved).
- No case-insensitive collision with an existing preset name (builtin or user).

Filename stem becomes the display name. Identity is case-insensitive: `My-Look.toml` and `my-look.toml` are the same preset.

## LUT files

LUTs live in `../LUTs/` (sibling of `Presets/`). Format: 32x32x32 RGBA8 (8 bpc) volume DDS. Reference by filename stem in `lut_path` (no `.dds` extension). Missing files log a warning once and the renderer falls back to the base LUT for the offending overlay.

## Marker file (smoke harness)

Drop `.imagespace_force_preset` next to `Imagespace.toml` with a single-line payload (either a `B:`/`U:` identity or a bare display name). On next plugin load the marker preset is applied and `auto_load_on_boot` is ignored for that run. Delete the file to release the override. See `scripts/smoke-imagespace-preset-load.sh` for an example.
