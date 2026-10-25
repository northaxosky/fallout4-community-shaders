# Rain Refraction

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Refractive rain draws whenever Enable is set and the texture and blend state exist | It also needs a published reflections cube; otherwise stock rain draws | The cube is unpublished before the first capture and while Dynamic Cubemaps is disabled |
| Pending | Clustered point lights shade the drops | Only the sun shades them | Needs Light Limit Fix |
| N/A | The Effects 11 preset owns the settings and supplies `enbraindrops.png` | Settings and the bundled texture only | FO4 has no Effects 11 |
