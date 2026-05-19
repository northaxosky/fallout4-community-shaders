# `D3DCompile` static analysis

## TL;DR

The Fallout 4 engine **does not use `D3DCompile`** under any path across OG/NG/AE: zero `d3dcompiler*` imports, zero d3dcompile-family strings in any of the three exes. The "engine compiles HLSL at runtime" hypothesis is definitively ruled out.

Three captured "missing" deferred-stage PSes were then matched against shape-and-size-near corpus peers, then dumped from RenderDoc and byte-diffed. **The byte-diff was misleading (46-53% of bytes differ) but the mnemonic-stream diff is conclusive**:

| Pair | Runtime insns / samples | Corpus insns / samples | Δ |
|---|---|---|---|
| composite eid 45368 vs blob 3539 | 90 / 6 | 90 / 6 | **0 / 0** |
| ambient/IBL eid 45345 vs blob 3559 | 263 / 44 | 265 / 44 | -2 / 0 |
| sun-shadow eid 45401 vs blob 2147 | 62 / 1 | 62 / 1 | **0 / 0** |

The corpus contains functionally-identical (or near-identical) versions of all 3 deferred-stage PSes. The byte-level differences are explained by register-allocation choices during DXBC encoding - same shader, different bytecode encoding. Resource declarations and control flow are identical between runtime and corpus peer.

## What this means for the deferred-lighting reconstruction

- The composite is corpus blob 3539 (`861504f6dcbe...`). The earlier 2122 classifier pick was wrong, but the canonical IS in the on-disk corpus.
- Canonical ambient/IBL is corpus blob 3559 (`7460585eaf76...`), the close peer of blob 3560 already analysed. Runtime-stream equivalence confirms the reconstruction target.
- The captured sun-shadow PS at eid 45401 matches corpus blob 2147 (`8fb709c2fdf0...`) exactly at instruction count + sample count. Blob 3147 is a different PS; the initial heaviest-representative pick for the directional-sun-light role doesn't match any captured PS in this scene.

Reconstruction can proceed directly against the corpus blobs. The previous "blocked on bytecode-source location" story is closed.

## What this means for the runtime catalog plugin

The `ShaderCatalog` plugin was originally going to hook `D3DCompile`. With `D3DCompile` ruled out AND the corpus-equivalence finding making patch-vector capture moot, the next-step shape changes:

- **Drop** the `D3DCompile` hook entirely.
- **Drop** the patch-vector idea (patches aren't the mechanism).
- **Future: corpus-match enrichment.** A `corpus_match_sha1` column on `shader_catalog`, populated by a writer-thread mnemonic-stream match against a shipped corpus DB. This joins runtime-loaded shaders to corpus blobs deterministically.

The current observe-only hook surface (the six `CreateXxxShader` vtable slots) remains correct and unchanged. The enrichment layer is purely additive (one column + one writer-thread routine) rather than a different hook surface.

## Per-shader entry points

Reconstruction work for each deferred-stage PS operates directly on the corpus blob:

1. **composite**: corpus blob 3539 (sha1 `861504f6dcbe...`).
2. **ambient/IBL**: corpus blob 3559 (sha1 `7460585eaf76...`); the close peer 3560 is interchangeable.
3. **sun-shadow / VLS slice**: corpus blob 2147 (sha1 `8fb709c2fdf0...`).
