# Rain Refraction

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Refractive rain draws whenever Enable is set and the texture and blend state exist | It also needs a published reflections cube; otherwise stock rain draws | The cube is unpublished before the first capture and while Dynamic Cubemaps is disabled |
| Forced | Clustered lights use upstream's light color and attenuation | Light Limit Fix's FO4 effect falloff, color decode and spot cone | Same light terms as other FO4 effects |
| N/A | The Effects 11 preset owns the settings and supplies `enbraindrops.png` | Settings and the bundled texture only | FO4 has no Effects 11 |
