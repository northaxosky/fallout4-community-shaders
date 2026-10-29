# Light Limit Fix

Enable `[features.LightLimitFix] load = true` and the `bsdf_light`,
`df_tiled_lighting`, `effect` and `bswater` `[shader_ownership]` targets, then restart.

Place authoring files in `Data\F4SE\Plugins\FO4CommunityShaders\Lights\*.toml`.
The FO4 format replaces Skyrim's LIGH extension bits and repurposed spot fields;
see [deviations](../../docs/deviations/LightLimitFix.md).
Native form flags and attenuation coefficients are not changed.
Explicit opt-outs retain native attenuation and color.

`[features.LightLimitFix.settings] derive_unauthored_lights` (default
`false`, applies at next launch) set to `true` converts every light with no
TOML entry from its native falloff when it is created; `false` leaves unlisted
lights native, so only authored lights are inverse-square.
A matching TOML entry, base form or reference, is authoritative:
only its fields apply, omitted fields take defaults with intensity scale 1,
and derived values are never merged with it.
Derivation fills only `inverse_square`, `size` and an intensity scale
(equivalent to retuning LIGH fade). `size` makes the half-intensity distance
match the native curve's (clamped to 0.01 to just below 50, the sqrt(2) sentinel).
The scale then matches the d^2-weighted energy of the native curve out to its
radius with that size. Derived lights keep the native radius. Lights stay native
when DATA flag 0x4 negates color, the radial scalar is 0, native energy is not
positive, or the radius is at most 0.1.

```toml
[[lights]]
plugin = "MyLights.esp"
form_id = 0x800
inverse_square = true
linear = true
cutoff = 1.0
size = 1.41421356

[[references]]
plugin = "MyLights.esp"
form_id = 0x801
inverse_square = false
```

`lights` identifies a LIGH base form; `references` identifies a placed reference.
`form_id` is a nonzero plugin-local ID, not a load-order-prefixed runtime ID.
Files merge in filename order; later supplied fields override earlier fields.
Reference fields override their base form and inherit omitted fields.
Invalid types, unknown fields and nonfinite numbers reject the document.
Missing or wrong-type forms are logged and ignored. Changes require restart.

| Field | Default | Meaning |
|---|---|---|
| `inverse_square` | `false` | Opts this light into inverse-square falloff |
| `linear` | `false` | Per-light linear-color flag |
| `cutoff` | `1.0` | Clamped to 0.01-1; exactly 1 selects 0.05 for nonshadow lights or 0.022 for shadow lights |
| `size` | sqrt(2) | Source size in meters; values >= 50 select sqrt(2), then clamp to 0.01-50 |

Intensity is the native animated dimmer times 4, times the derived scale.
Spot cones, cookies, shadow tests, hemisphere/box masks and native gameplay
exclusions remain independent.
