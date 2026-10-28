# Sky Sync

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Volumetric lighting intensity, color and night blend | Godray intensity and medium from the weathers' night godray slots, for both the dim and the night rebuild | FO4 has NVIDIA godrays, not volumetric lighting |
| Forced | Sun arc peak 400 and sideways offset 25 | `fSunXExtreme` and `fSunYExtreme`; alternate-path horizon distance keeps the 0.7 ratio | `Sun::Update` reads them |
| Forced | Moon phase from the texture name; moons move on their own tracks | Phase from the day count, shared by both moons; both moons follow the sun's arc, so Masser and Secunda coincide in a climate that enables both | `Moon::Update` has no per-moon motion |
| Forced | Interior cells need one sunlight-shadows flag | Interior cells need both sun-shadow cell bits; a sky mode of none is skipped | Main sun-shadow eligibility |
| N/A | EVLaS, DVLaSS and Moon and Stars conflict checks | Omitted | Skyrim-only mods |
| N/A | Default disabled worldspaces: Blackreach, Alftand, Sovngarde, Soul Cairn, Boneyard, Apocrypha | Empty list | No FO4 worldspace keeps one light around the clock |
| N/A | `LinearLighting` sunlight color path | Omitted | Upstream never assigns its colors; FO4 sunlight is already linear |
| Pending | Celestial direction and moon-light caster queries, moon night weights | Not exposed | Their consumers (Effects11 grading, shared lighting moon colors) are not ported |

## Upstream bugs

- `SkySync::Update`: disabling the feature leaves the sky root rotated by the last cell's north rotation.
