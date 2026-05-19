# LUTs

Drop 32x32x32 RGBA8 DDS volume textures here. Set `sLUTPath` in `Imagespace.ini` to the filename without extension (e.g. `sLUTPath = neutral`) and toggle `bLUTEnable = true`.

Authoring flow: grade in DaVinci Resolve (or any tool that emits `.cube`), then run `texassemble cube` from DirectXTex to convert the cube file into a `.dds` volume texture at 32x32x32. Files of other dimensions are rejected at load time and the LUT path falls back to bypass for the session.
