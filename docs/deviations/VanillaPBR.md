# Vanilla PBR

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Material shininess and specular-roughness mapping | Native gloss and directional wetness; SpecularRoughnessBlend defaults to peak-matched roughness | FO4 G-buffer inputs |
| Divergence | Material F0 mapping | Dielectric F0 is max(MinF0, BaseF0Multiplier × specular intensity), defaults 0.02 and 0.2 | FO4 stores monochrome specular intensity and no metalness; 0.2 × intensity is vanilla's normal-incidence Fresnel |
| Forced | Skin and Pip-Boy screen | Classes 5 and 4 keep FO4's intensity-masked GGX lobe at F0 BaseF0Multiplier | Their specular is tuned for the native mask; the skin model is not yet ported |
| Forced | Eye material identity and response | A spare prepass class tags eyes for upstream fixed F0/roughness in direct light | FO4's stock G-buffer had no deferred eye identity |
| Forced | Water material classes | Water remains subject to the class stamped by the final composite, and can fall through the ordinary indirect path | Deferred lighting cannot see the later water class stamp |
| Forced | Ordinary material gloss and intensity | Hair keeps native lobes | Hair G-buffer RGB stores a direction instead |
| Pending | Grass special handling | Not implemented | FO4's G-buffer has no reliable deferred grass identity |
| N/A | Complex-material F0 | | FO4 has no complex-material input |
