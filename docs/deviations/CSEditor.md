# CS Editor

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Weathers labeled by name, editor ID or decimal FormID | `EditorID (0xLocalID~Plugin)`, the per-weather TOML key | FO4 weathers have no full name; the key pastes into `[features.<key>.weather]` |
| Forced | Type filters: Pleasant, Cloudy, Rainy, Snow, Aurora, Aurora Sun | Same bits labeled Pleasant, Cloudy, Rainy, Dusty, Radstorm, Radstorm Sun; radstorm outranks dust for color | Vanilla FO4 sets the Snow bit only on Glowing Sea haze, dust and radstorms, and the aurora bits only on radstorms |
| Divergence | Time-sensitive menus: Loading, SleepWait, Map | Loading, SleepWait, Pipboy | FO4 fast travel starts from the Pip-Boy map |
| Divergence | Weather overrides edited in the Weather Widget, stored as per-weather JSON | Edited on the CS Editor page, stored in each feature's `[features.<key>.weather]` TOML map | Reuses the typed weather profiles |
| Divergence | Lightning color uses a color editor | Three 0-255 sliders | The forwarding UI has no color editor |
| Divergence | Wind panel shows wind relative to the player using a 30.5 degree offset | Player heading only | The offset is Skyrim-calibrated |
| Pending | Game-hour scrubbing requests a time-jump transition | Not ported | |
| Pending | Weather detail overlay, editor window shell, form widgets, light editor, palette, interior-only panel | Not ported | |
