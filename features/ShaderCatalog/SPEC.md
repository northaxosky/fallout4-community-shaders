# ShaderCatalog feature

Observes shader creation on `ID3D11Device` and records each unique shader to a per-session SQLite catalog. Read-only with respect to the engine; the hot path is `CreateXxxShader -> SHA1 -> SQLite upsert -> chain to original vtable entry`.

## Hook surface

Vtable detours on `ID3D11Device`:

- Slot 12: `CreateVertexShader`
- Slot 13: `CreateGeometryShader`
- Slot 15: `CreatePixelShader`
- Slot 16: `CreateHullShader`
- Slot 17: `CreateDomainShader`
- Slot 18: `CreateComputeShader`

The detour wraps the call, SHA1s the bytecode, then chains to the original entry. Hooks are installed once at device creation and never removed.

## SHA1 algorithm

Standard SHA1 over the DXBC blob passed to `CreateXxxShader` (the `pShaderBytecode` / `BytecodeLength` pair, byte-for-byte). The hex digest is the catalog key.

## Storage

SQLite at `Data/F4SE/Plugins/FO4CommunityShaders/shader-catalog.sqlite`. Schema is embedded verbatim in `CatalogDB.cpp`. WAL journaling. Key tables:

- `shader_catalog` (sha1, stage, size_bytes, source_module, creation_thread_id, engine_runtime, ...)
- `sessions` (per-process-launch row; `ended_at` is updated on clean shutdown, left NULL on crash)
- `corpus_meta` (`schema_version`, etc.)

## Implemented

`Create{Vertex,Pixel,Geometry,Hull,Domain,Compute}Shader` vtable detours + SHA1 + SQLite catalog + ImGui menu surface.

### Subclass attribution (Path B)

Each known concrete `BSShader` subclass has these vtable slots patched at feature `Load()`:

- Slot `0x0B`: `ReloadShaders(bool)` for explicit shader reload attribution.
- Slot `0x02`: `SetupTechnique(uint32_t)` for runtime attribution during normal rendering.

`CreatePixelShader` stores a retained `ID3D11PixelShader* -> sha1` side table for engine-created shaders. After the original `SetupTechnique` returns true, the thunk probes `self->pixelShaders` by technique id, resolves the bound D3D pixel-shader pointer through that side table, and enqueues an attribution-only SQLite upsert. This fills `bsshader_subclass` and `bsshader_technique_bits` without changing engine D3D calls.

Subclasses hooked (12): `BSBloodSplatterShader`, `BSDFCompositeShader`, `BSDFLightShader`, `BSDFPrePassShader`, `BSDistantTreeShader`, `BSEffectShader`, `BSFaceCustomizationShader`, `BSLightingShader`, `BSParticleShader`, `BSSkyShader`, `BSUtilityShader`, `BSWaterShader`.

Attribution events are UPSERTs, not UPDATE-only records. If an attribution reaches the writer before the original create row, SQLite inserts a placeholder PS row with `size_bytes=0`; a later create-row UPSERT fills the bytecode metadata. `seen_count` is not incremented by attribution-only events. Normal create-row UPSERTs preserve the first non-null `bsshader_subclass` and `bsshader_technique_bits` values via `COALESCE`.

`ID3D11DeviceContext::PSSetShader` is also hooked as a fallback for cases where the engine binds inside or shortly after a `SetupTechnique` scope, but runtime validation showed FO4 often uses its own state cache and does not call `PSSetShader` for every setup. The `pixelShaders` map probe is the primary path.

Runtime validation on OG-Testing with ShaderReplacement forced off attributed 50 of 1467 PS rows across 8 subclasses. Partial attribution is expected: only permutations exercised by the current scene and routed through known `BSShader::SetupTechnique` surfaces are attributed. Zero attributed PS rows is a failure.

PS rows created outside known `BSShader` subclasses, such as `BSImagespaceShader` in FO4's separate imagespace hierarchy or non-engine creators, remain NULL and surface as unattributed in the ImGui stats counter.

ShaderReplacement interop: when a runtime PS is substituted, the replacement `ID3D11PixelShader*` is registered as an alias for the original runtime sha1 so later attribution still lands on the engine-originated catalog row. The alias tracker is enabled only while ShaderCatalog is active.

### Technique ids

`bsshader_technique_bits` stores the `SetupTechnique(uint32_t)` argument. For some subclasses this is a bitfield-like technique selector; for others it is an opaque technique id that keys the subclass's `pixelShaders` map. Keep the column nullable because not every PS row can be attributed.

## Not implemented

- D3DCompile hook removed; the engine does not call D3DCompile under any path (confirmed via import-table audit across OG/NG/AE: zero `d3dcompiler*` imports, zero d3dcompile-family strings). The `compile_events` table stays in the schema for forward-compat.
- Future: corpus-match enrichment. A `corpus_match_sha1` column on `shader_catalog`, populated by a writer-thread mnemonic-stream match against a shipped corpus DB. Schema v2 territory; needs a C++ DXBC mnemonic disassembler.
- Future: exact decoder labels for `bsshader_technique_bits` by subclass. Fallout4RE exports include `BSDFLightShaderMacros::GetPixelShaderID` and peers; use those when a feature needs semantic names rather than raw ids.
- Future: `BSImagespaceShader` attribution. The class is not in the BSShader virtual hierarchy in FO4, so vtable slot 0x0B patching does not apply. A separate hook on the imagespace shader loader is needed.
- Future: catalog import. Workspace-side merger to fold per-session catalogs into a long-lived analysis DB.
- `cs::ShaderCache` substitution path. The catalog is the lookup table, not the substitution layer.

## Smoke-harness gates (run before declaring the feature shippable)

1. Fresh install + `enabled = true` + game boot: `shader-catalog.sqlite` is created and contains `corpus_meta.schema_version=1`, a `sessions` row, and `PRAGMA journal_mode -> wal`.
2. After main-menu entry + exit: `shader_catalog` has rows with non-NULL `sha1`, `stage`, `size_bytes`, `source_module`, `creation_thread_id`, `engine_runtime`.
3. After one in-game scene load: catalog contains at least one PS row and `attributed_ps > 0` for known `BSShader` subclasses exercised by the scene.
4. Crash test: kill `Fallout4.exe` via Task Manager mid-session; reopen game; previous session has `ended_at IS NULL` and rows survive (WAL replay).
5. Cross-runtime: smoke gates 1-3 against each of OG/NG/AE binaries.
