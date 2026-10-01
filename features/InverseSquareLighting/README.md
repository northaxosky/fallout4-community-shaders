# Inverse Square Lighting

Enable `[features.InverseSquareLighting] load = true` and the `BSDFLight` and
`DFTiledLighting` shader ownership targets, then restart. There are no global
strength, source-size or interior/exterior settings in upstream ISL.

Place authoring files in `Data\F4SE\Plugins\FO4CommunityShaders\Lights\*.toml`.
The FO4 format replaces Skyrim's occupied LIGH extension bits and repurposed
spot fields; see [DEVIATIONS](../../docs/DEVIATIONS.md#inversesquarelighting).
Neither native form flags nor native attenuation coefficients are changed.
Unlisted lights and explicit opt-outs retain native attenuation and color.

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

| Field | Default | Upstream interpretation |
|---|---|---|
| `inverse_square` | `false` | Opts this light into ISL |
| `linear` | `false` | Upstream per-light linear-color flag |
| `cutoff` | `1.0` | Clamped to 0.01–1; exactly 1 selects 0.05 for nonshadow lights or 0.022 for shadow lights |
| `size` | √2 | Source size in meters; values ≥50 select √2, then clamp to 0.01–50 |

Intensity is the native animated dimmer times 4. Radius and the 252-unit
fade zone follow upstream. Render publication includes native `currentFade`;
gameplay luminance intentionally does not. Spot cones, cookies, shadow tests,
hemisphere/box masks and native gameplay exclusions remain independent.

The native tiled list stays authoritative: t8 metadata follows the same dense
accepted-append index and buffer side as t6. Raster local draws receive b11
metadata. Native radii are restored when lights are removed or the feature is
quarantined. Enlarged-radius tile culling guards all 10–25 group dimensions
against writing beyond 127 indices; the native count and lighting clamp remain.

Upstream's balanced intensity/cutoff/size case produces zero radius, leading to
infinite reciprocal radius/fade-zone inputs. It is retained for parity and
recorded as an upstream PR candidate, not silently corrected.

## Runtime validation

Use the separately authorized batched DevBench session; do not infer rendering
or native-hook success from offline tests. AE is advertised; OG/NG need their
own observations.

- Compare authored/unlisted pairs and reference opt-out against a feature-off
  restart. Check actual shader execution, not only installed-hook telemetry.
- Correlate reference → LIGH → NiLight → BSLight → t6/t8 index and buffer side
  across consecutive frames, culling, removal, reorder and side swaps.
- Trace point/spot/shadow/gobo/attenuation-only raster draws through b11.
  Confirm directional lighting, cones, cookies, shadows and material responses
  remain independent; include flicker/pulse and negative-color lights.
- Inspect all three native radius channels, spot cone bounds, frustum edges and
  raster volumes during animated dimmer changes and camera motion.
- Overlap 128–625 lights in one tile. Verify count, bounded indices and adjacent
  memory for active cull dimensions; bounded storage does not guarantee that
  every overlapping light renders.
- Compare native point luminance and cached/normalized actor light level against
  intensity/distance. Check self-light exclusion, shape masks, detection-radius
  tests and collision rays; render currentFade must not enter gameplay.
- Exercise disable/delete, cell detach/reload, save reload and quarantine.
  Inspect restored radii/PS b11/CS t8, released owners, and zero upload failures
  with RenderDoc and telemetry. First-use raster fallback remains unverified.
