# Vanilla PBR

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Native environment reflection plus Fresnel environment lobe | Envmapped pixels keep FO4's envmap formula, with Dynamic Cubemaps' sharp cube in place of the authored one; non-envmapped pixels keep native indirect light | FO4's authored envmaps are pre-blurred paintings that never show the world, and its formula already carries their strength and local light |
| Divergence | Material shininess and specular-roughness mapping | Native gloss and directional wetness; SpecularRoughnessBlend defaults to peak-matched roughness | FO4 G-buffer inputs |
| Divergence | Material F0 mapping | Dielectric F0 is max(MinF0, BaseF0Multiplier × specular intensity), defaults 0.02 and 0.2 | FO4 stores monochrome specular intensity and no metalness; 0.2 × intensity is vanilla's normal-incidence Fresnel |
| Divergence | Envmapped reflection strength | The live sample is normalized to its cube's average luminance (six last-mip axis samples) times the authored cube's, at stock mip footprint | Native weighting multiplies the cube by local diffuse light, so the cube must be a pattern at authored brightness |
| Divergence | Envmapped reflection light | Live samples are weighted by the lighting passes' ambient diffuse (DALC, IBL, Skylighting), capped at native; authored cubes and screen-space reflections keep the native total diffuse | Native weighting includes direct sun and local lights, whose reflection is the direct highlight; sunlit gloss washes out |
| Divergence | Envmapped reflection occlusion | Live samples take Lagarde specular occlusion from native SSAO times SSGI visibility (SSGI's own specular occlusion where it composes the cube) instead of native SSAO's linear factor | Native reflections are unoccluded by SSGI, so live reflections glow in crevices whose diffuse SSGI darkens |
| Divergence | Envmapped reflection color | The live sample takes the authored cube's luminance-normalized color for warm cubes (red ≥ blue), from none at saturation 0.40 to full at 0.70 | Metal tint is authored in bronze, copper and gold cubes; generic and chrome cubes are near neutral and blue casts are scene illumination |
| Divergence | Projected decals | Decals follow their G-buffer data: near-unit gloss and intensity, engine envmap scale and default cube, so puddles reflect at vanilla's strength with live content | FO4 decals carry no puddle or wetness marker |
| Pending | Rain wetness | Rain keeps FO4's wet gloss, intensity and envmap scale; live samples skip its cube desaturation | The live cube already shows the overcast sky; upstream's wetness model is not yet ported |
| Forced | Skin and Pip-Boy screen | Classes 5 and 4 keep FO4's intensity-masked GGX lobe at F0 BaseF0Multiplier; their envmaps follow the live cube like any other | Their specular is tuned for the native mask; the skin model is not yet ported |
| Forced | Eye material identity and response | A spare prepass class tags eyes for upstream fixed F0/roughness in direct light; eyes keep their authored envmap reflection | FO4's stock G-buffer had no deferred eye identity |
| Forced | Water material classes | Water remains subject to the class stamped by the final composite, and can fall through the ordinary indirect path | Deferred lighting cannot see the later water class stamp |
| Forced | Ordinary material gloss and intensity | Hair keeps native lobes | Hair G-buffer RGB stores a direction instead |
| Pending | Grass special handling | Not implemented | FO4's G-buffer has no reliable deferred grass identity |
| N/A | Complex-material F0 | | FO4 has no complex-material input |
