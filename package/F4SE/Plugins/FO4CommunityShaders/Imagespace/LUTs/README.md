# LUTs

Drop 32x32x32 RGBA8 DDS volume textures here. Set `lut_path` under `[settings]` in `Imagespace.toml` to the filename without extension (e.g. `lut_path = "neutral"`) and toggle `lut_enable = true`.

Authoring flow: grade in DaVinci Resolve (or any tool that emits `.cube`), then run `texassemble cube` from DirectXTex to convert the cube file into a `.dds` volume texture at 32x32x32. Files of other dimensions are rejected at load time and the LUT path falls back to bypass for the session.
