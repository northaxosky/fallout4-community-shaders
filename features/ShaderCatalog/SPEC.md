# ShaderCatalog feature

Canonical spec lives in the Fallout4RE repo and is authoritative:

- `Fallout4RE/Workspace/docs/shader-catalog-plugin-spec.md` (lifecycle, hot-path invariant, INI, ImGui surface).
- `Fallout4RE/Workspace/docs/d3d11-device-vtable-map.md` (hook surface; slots 12/15/16/17/18 on `ID3D11Device`).
- `Fallout4RE/Workspace/schemas/runtime/shader-catalog.sqlite.schema.sql` (DB schema; embedded verbatim in `CatalogDB.cpp`).

## Implemented here

- Phase 1: `Create{Vertex,Pixel,Geometry,Hull,Domain,Compute}Shader` vtable detours (slots 12/13/15/16/17/18) + SHA1 + SQLite catalog + ImGui menu surface.

## Not implemented (deferred to later phases or out of scope entirely)

- ~~Phase 2 (`D3DCompile` HLSL-source capture)~~ - **dropped 2026-05-18**. The Fallout4RE-side `docs/d3dcompile-static-analysis.md` proved `D3DCompile` is never called by FO4 (zero imports, zero strings across OG/NG/AE) AND the previously-missing deferred PSes are in the on-disk corpus under different sha1 (DXBC re-encoded by D3D loader). The compile-hook surface is removed; the `compile_events` table stays in the schema for forward-compat with a reframed Phase 2.
- **Phase 2 (reframe)**: `corpus_match_sha1` column on `shader_catalog`, populated by writer-thread mnemonic-stream match against `Fallout4RE/.cache/shader-corpus.sqlite`. Schema v2 territory; needs a C++ DXBC mnemonic disassembler. Separate prompt.
- Phase 3 (`BSShader` subclass `LoadShaders` enrichment for `bsshader_subclass` / `bsshader_technique_bits` columns).
- Phase 4 (`import-runtime-catalog.py` workspace merger; lives Fallout4RE-side).
- `cs::ShaderCache` substitution path (separate prompt; catalog is the lookup table, not the substitution layer).

## Smoke-harness gates (run before declaring the feature shippable)

1. Fresh install + `bEnabled=true` + game boot: `shader-catalog.sqlite` is created and contains `corpus_meta.schema_version=1`, a `sessions` row, and `PRAGMA journal_mode -> wal`.
2. After main-menu entry + exit: `shader_catalog` has rows with non-NULL `sha1`, `stage`, `size_bytes`, `source_module`, `creation_thread_id`, `engine_runtime`.
3. After one in-game scene load: catalog contains at least one PS row (DXBC-parsed shape fields stay NULL in Phase 1; the workspace importer fills them at merge time).
4. Crash test: kill `Fallout4.exe` via Task Manager mid-session; reopen game; previous session has `ended_at IS NULL` and rows survive (WAL replay).
5. Cross-runtime: smoke gates 1-3 against each of OG/NG/AE binaries.
