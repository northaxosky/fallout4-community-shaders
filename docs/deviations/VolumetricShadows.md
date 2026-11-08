# Volumetric Shadows

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| N/A | ESRAM shadow map min-ed into the moments and resized to 1024 | Moments come from the sun cascades alone | FO4 has no second directional shadow map |
| Divergence | Two cascades; the fade past the second returns lit | With three splits the soft shadow fades into the native cascade shadow, so the third cascade keeps it; with one split cascade 1 reads lit | FO4 renders one to three sun cascades |
| Forced | VSM soft shadow on every directional pass | Only the shadowed families (one to three splits); unshadowed sun passes keep unshadowed lobes | FO4 renders no cascades for them |
| Forced | Skylighting visibility replaces the VSM soft term per material lighting flag | Same gate with FO4 channels: it replaces it where `backLightPower` (MRT0.a) or subsurface rolloff (MRT3.z) is nonzero while Skylighting is enabled; other pixels keep the VSM | No rim flag reaches the light pass |
| Forced | Water sun glint and refracted diffuse sun shadowed along the refracted ray | Sun specular and the fixed-axis lobe are shadowed along the unrefracted view ray | FO4 water has no refraction direction |
| N/A | Refracted diffuse sun term and its short rays | | FO4 water has no diffuse sun term |
| N/A | `BSParticleShader` receiver | | FO4 never draws the particle shader |
