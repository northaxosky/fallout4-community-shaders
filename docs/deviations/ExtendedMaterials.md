# Extended Materials

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | Consumers in `Lighting.hlsl` (`PARALLAX`, `LANDSCAPE`) | Marcher runs in the `DFPrepass` pixel stage and offsets albedo, normal and `_s` for textured objects and non-instanced terrain; object LOD, tessellated variants and OG blends excluded | FO4's world frame is deferred |
| Forced | Height in the `_p` slot (objects) and diffuse alpha (terrain) | Height read from the `_s` alpha; objects compile without `PARALLAX`, so the march samples one mip coarser | FO4 parallax packs store height in the `_s` alpha |
| Forced | `PARALLAX` technique marks parallax materials | Objects admitted when the `_s` format stores alpha (BC2, BC3, BC7, RGBA8, BGRA8), except decals and alpha-tested overlays; a map whose mip chain stops above 4 texels or whose last mip averages alpha 1 gets `HeightScale` 0, per object and per landscape layer | BGSMs carry no parallax marker; FO4 parallax content follows ENB's `_s` alpha convention, and landscapes mix height maps with BC1/BC5 layers |
| Forced | Six landscape layers | Four normalized weights, layers 5-6 at zero | FO4 landscape has four layers |
| Forced | Terrain Helper displacement layers | Descriptor is 0; terrain parallax follows `EnableTerrain` alone | FO4 has no Terrain Helper |
| Forced | "Enable Legacy Terrain", off by default | "Enable Terrain Parallax", on by default | FO4 has no Terrain Helper, and layers without height stay flat, so mixed landscapes are safe |
| Divergence | Relief depth of 0.1 UV (`maxHeight`) | Every admitted height map gets `HeightScale` 0.5; objects take the same factor | FO4 landscape spans twice Skyrim's world units per UV |
| Divergence | Parallax soft shadows evaluated in forward lighting (`EnableShadows`) | Prepass writes the sun multiplier to an R8 target at `SV_Target6`; DFLight multiplies the sun shadow by it at t66; the terrain sun-angle gate uses the geometric normal | FO4 lights from the G-buffer |
| N/A | Point-light parallax soft shadows | Not applied | Each light needs its own tangent-space march, which G-buffer lighting cannot repeat |
| N/A | `bLandSpecular` INI enable | Not applied | FO4 landscape always binds a separate `_s` map |
| Pending | Complex Material (`EnableComplexMaterial`) | Published as 0 | FO4's envmap loader has no separate mask |
| Pending | TruePBR surface POM | Not applied | Needs the TruePBR material loader |
