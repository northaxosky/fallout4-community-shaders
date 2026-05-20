# ShaderCatalog feature

Observes shader creation on `ID3D11Device` and records each unique shader to a per-session SQLite catalog. Read-only with respect to the engine; the hot path is `CreateXxxShader -> SHA1 -> SQLite upsert -> chain to original vtable entry`.

## Hook surface

Vtable detours on `ID3D11Device`:

- Slot 12: `CreateVertexShader`
- Slot 13: `CreatePixelShader`
- Slot 15: `CreateGeometryShader`
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

Each concrete `BSShader` subclass has its vtable slot `0x0B` (`ReloadShaders`) patched at feature `Load()`. The patched thunk pushes a thread-local `subclass_name` scope and chains to the original (shared) base implementation, which iterates the subclass's `*.fxp` technique permutations and calls `ID3D11Device::CreatePixelShader` per blob. The device-vtable hook reads the TLS context and stamps the `bsshader_subclass` column.

Subclasses hooked (12): `BSBloodSplatterShader`, `BSDFCompositeShader`, `BSDFLightShader`, `BSDFPrePassShader`, `BSDistantTreeShader`, `BSEffectShader`, `BSFaceCustomizationShader`, `BSLightingShader`, `BSParticleShader`, `BSSkyShader`, `BSUtilityShader`, `BSWaterShader`.

On UPSERT, `bsshader_subclass` is preserved via `COALESCE`: the first non-null attribution wins. PS rows created outside any hooked `ReloadShaders` (e.g. from `BSImagespaceShader`, which is not in the BSShader hierarchy in FO4, or from non-engine creators) remain NULL and surface as unattributed in the ImGui stats counter.

### Technique bits (gap)

`bsshader_technique_bits` is plumbed end-to-end (TLS context field, `CatalogEntry` field, SQL binding, `COALESCE` upsert) but currently always written as NULL because `ReloadShaders(bool)` does not carry the per-permutation technique-bit value as a parameter. Recovering tech-bits requires either:

- Hooking a deeper helper (`BS{Subclass}::GetPixelShaderID` / equivalent) that takes the technique-bit integer as input and resolves to a `BSGraphics::PixelShader*`, then correlating to sha1 via a side-table populated by the device-vtable hook, or
- Hooking `SetupTechnique(uint32_t)` on each subclass for retroactive update of rows whose sha1 the engine has bound under that technique bit.

Both are follow-up work; the column stays nullable and `COALESCE` semantics let a later writer fill it in without schema migration.

## Not implemented

- D3DCompile hook removed; the engine does not call D3DCompile under any path (confirmed via import-table audit across OG/NG/AE: zero `d3dcompiler*` imports, zero d3dcompile-family strings). The `compile_events` table stays in the schema for forward-compat.
- Future: corpus-match enrichment. A `corpus_match_sha1` column on `shader_catalog`, populated by a writer-thread mnemonic-stream match against a shipped corpus DB. Schema v2 territory; needs a C++ DXBC mnemonic disassembler.
- Future: per-permutation technique-bit attribution. See "Technique bits (gap)" above.
- Future: `BSImagespaceShader` attribution. The class is not in the BSShader virtual hierarchy in FO4, so vtable slot 0x0B patching does not apply. A separate hook on the imagespace shader loader is needed.
- Future: catalog import. Workspace-side merger to fold per-session catalogs into a long-lived analysis DB.
- `cs::ShaderCache` substitution path. The catalog is the lookup table, not the substitution layer.

## Smoke-harness gates (run before declaring the feature shippable)

1. Fresh install + `bEnabled=true` + game boot: `shader-catalog.sqlite` is created and contains `corpus_meta.schema_version=1`, a `sessions` row, and `PRAGMA journal_mode -> wal`.
2. After main-menu entry + exit: `shader_catalog` has rows with non-NULL `sha1`, `stage`, `size_bytes`, `source_module`, `creation_thread_id`, `engine_runtime`.
3. After one in-game scene load: catalog contains at least one PS row (DXBC-parsed shape fields stay NULL until the corpus-match enrichment lands).
4. Crash test: kill `Fallout4.exe` via Task Manager mid-session; reopen game; previous session has `ended_at IS NULL` and rows survive (WAL replay).
5. Cross-runtime: smoke gates 1-3 against each of OG/NG/AE binaries.
