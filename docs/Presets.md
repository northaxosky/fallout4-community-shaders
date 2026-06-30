# Presets

A **preset** is a single TOML file that captures the tunable state of every
participating Community Shaders feature in one place. Loading a preset replaces
the live state of those features atomically; sharing a preset is just sharing
the file.

## What's included

As of schema version 1, presets cover:

| Feature key | Feature |
|---|---|
| `imagespace` | tonemap, bloom, vignette, sharpen, LUT, weather profiles |

Each feature contributes its own `[features.<key>.settings]` subtable. Imagespace
additionally contributes `[features.imagespace.weather]` for per-weather overlays.
Other features may be added in later schema versions; older presets continue to
load (unknown tables are ignored).

## Where presets live

```
Data\F4SE\Plugins\FO4CommunityShaders\Presets\
  Builtin\         shipped, read-only (Default, Neutral-Realistic, ...)
  *.toml           your own user presets
```

User preset names are case-insensitive; saving a name that collides with a
builtin (or another user preset) is rejected. Names must match
`[A-Za-z0-9_-]{1,64}` and must not be a Windows reserved device name
(CON, PRN, AUX, NUL, COM1..9, LPT1..9, CLOCK$) or the literal "Builtin".

## Using presets in-game

Open the menu (END key), expand **Presets** at the top:

- **Combo**: lists every preset, builtin (`B:`) first then user (`U:`).
- **Load**: flushes any unsaved live edits, then applies the selected preset.
- **Save**: overwrites the active user preset. Disabled when no user preset is
  active.
- **Save As...**: validates the name, then writes a new user preset with the
  current live state of every participating feature.
- **Delete**: removes the active user preset. Disabled for builtins.
- **Refresh**: re-scans the Presets folder. Useful after dropping a file in
  manually.
- **Auto-load on boot**: when on, the active preset is re-applied at game start
  (after every feature has parsed its TOML).

The status line shows the active identity (`Active: <name> (builtin|user)`).
A preset that vanished between sessions stays visible as
`(<name>) (missing)` until you pick another.

## Writing a preset by hand

```toml
# Data\F4SE\Plugins\FO4CommunityShaders\Presets\MyLook.toml
[meta]
schema_version = 1
name           = "MyLook"
created_by     = "you"
description    = "warm midtones, light bloom"

[features.imagespace.settings]
tonemap_operator = 1        # 1 = Hable, 2 = Reinhard, 3 = Lottes
bloom_intensity  = 0.05
vignette         = 0.3
lut_enable       = true
lut_path         = "Reactor-Warm"

[features.imagespace.weather.rain]
bloom_intensity = 0.06
vignette        = 0.35
```

### Schema rules

- `[meta].schema_version` is required and must currently be `1`.
- Each `[features.<key>]` subtable is optional; missing features keep their
  live state at apply time.
- Unknown keys and unknown subtables under any `[features.<key>]` are ignored
  silently.
- For builtin presets, any `[features.imagespace.weather.overrides]` table
  (the formID-to-category mapping) is dropped on load so shipped presets do
  not stamp your formID mappings. User presets honour overrides round-trip.

### LUTs

`lut_path` is a stem only (no extension, no folder). LUTs are resolved against
`Data\F4SE\Plugins\FO4CommunityShaders\Imagespace\LUTs\<stem>.dds`. Missing
LUTs log once and fall back to identity, no crash.

## Smoke marker

For automated testing, dropping a one-line `.cs_force_preset` file under
`Data\F4SE\Plugins\FO4CommunityShaders\` forces a one-shot apply at boot.
Payload is either a `<scope>:<name>` identity (`B:Default`, `U:MyLook`) or a
bare name (resolved with user preference). An invalid payload clears the
active preset and skips auto-load for that run.
