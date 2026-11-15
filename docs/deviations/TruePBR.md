# TruePBR

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Divergence | Mesh material data comes from a NIF shader type and per-NIF overrides | BGSM version 100: a vanilla v2 material (Part 1) followed by CS blocks (Part 2); `materialPath` carries the record name from the parsed template to the lighting material | FO4 BGSMs override NIF shader data, and the engine never uses `materialPath` |
| Pending | Texture sets use editor-ID JSON | Not read yet; a texture set that names a version 100 BGSM inherits its record | Texture-set support follows the shading work |
| Pending | Skin, face and hair materials | A vanilla part flagged skin tint, facegen or hair renders as Part 1 | Actor tint pipelines are separate |
