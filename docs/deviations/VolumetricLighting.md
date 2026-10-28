# Volumetric Lighting

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Patches Skyrim's froxel volumetric lighting: size globals, `SetVLQuality`, single-pass raymarch dispatch, generate/raymarch/blur/apply shaders | Controls the engine's NVIDIA GFSDK godrays; shaders stay library-owned | FO4 has no froxel volumetric lighting |
| Divergence | Low/Medium/High/Custom set the froxel volume width, height and depth | Low/Medium/High/Ultra/Custom set the godray buffer resolution relative to output resolution, MSAA, ray size and upsample filter | The GFSDK buffer is a power-of-two fraction of render resolution, so levels compensate render scale to stay output-relative like a fixed volume |
| Forced | Interior volumetric lighting requires an interior with sun | Interior settings apply to every interior godray emitter | FO4 interiors emit godrays from shadowed local lights |
| Forced | Enabling takes effect live | Enabling godrays when the game started with them off needs a restart | The library and its depth target exist only if enabled at startup |
| Pending | Sky Sync dimming, CS Editor volumetric lighting widget | Not ported | Features not ported |
