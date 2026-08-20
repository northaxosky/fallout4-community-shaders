# Deferred shader reconstructions

`package/Shaders/` holds reconstructed HLSL for Fallout 4's deferred renderer.
`src/Render/ShaderInjection.cpp` compiles registered permutations at runtime and
injects them in place of the corresponding stock shaders.

These files are live product content. Changes affect shaders distributed with
the plugin; they are not reference copies.

## Layout

Each engine shader class has one public, class-named `.hlsl` entry point:

- `BSDFLightShader.hlsl` contains the reconstructed BSDFLight families.
- `BSDFCompositeShader.hlsl` contains the reconstructed BSDFComposite families.
- `BSSkyShader.hlsl`, `BSWaterShader.hlsl`, and `BSLightingShader.hlsl` remain
  single files per engine class.
- `Common/` contains helpers shared by more than one entry point or family.

Feature shaders live in `features/<Name>/Shaders/<Name>/` and deploy alongside
these into `Data/Shaders/<Name>/`.

Each BSDF source uses one named `BSDFLIGHT_*` or `BSDFCOMPOSITE_*` block
selector per registered permutation. Native feature macros specialize the
selected block.

## Shared substrate

`Common/SharedData.hlsli` declares the per-frame data every injected shader can
read: `cbuffer SharedData : register(b5)` and `cbuffer FeatureData : register(b6)`.

`#ifdef FO4CS_SUBSTRATE` is the outermost gate, outside the include guard, so a
shader that includes the header without the define sees zero declarations.
`src/Render/ShaderInjectionCompileRequest.cpp` injects `FO4CS_SUBSTRATE=1`
automatically whenever a stage-matching `ShaderReplacementRegistration` exists
for the compiled target — including a contribution that only binds a resource.
Baseline ownership and developer force-on do not define it.

`FeatureData` is a static typed layout: every contributor field exists whether or
not its feature loaded, and `src/FeatureBuffer.cpp` zeroes the block of any
feature that is unloaded, unhealthy or not ready. `src/Render/SharedData.cpp`
owns both buffers, creates them during D3D11 bootstrap, refreshes them once per
engine frame at the post-prepass anchor, and binds b5/b6 only for injected pixel
draws. The previous pixel bindings are saved at deferred-lights entry and
restored at deferred-lights exit.

An archive-wide SHEX declaration census of the pinned shader archive leaves
only `b3`-`b8`, textures from `t16`, and no sampler slots for the plugin; the
archive has no RDEF chunks, and the result does not cover runtime-compiled
shaders or other mods. `b5` and `b6` are reserved: a contributor that claims
either as a constant buffer quarantines its target.

## Entry points

| Entry point | Engine shader or pass | Principal bindings |
|---|---|---|
| `BSDFLightShader.hlsl` | `BSDFLightShader` deferred lighting, hosted by `DrawWorld::DeferredLightsImpl` and the directional-light accumulation path | Reads the lighting G-buffer aliases and main depth; writes `kDiffuseBuffer=58` and `kSpecularBuffer=59`. |
| `BSDFCompositeShader.hlsl` | `BSDFCompositeShader`; ambient, image-based lighting, and composite families | Bindings and outputs vary by selected family. |
| `BSDFPrePass.hlsl` | G-buffer fill hosted by `DrawWorld::DeferredPrePass` | Writes six MRT outputs: albedo, octahedral normal, material data, two auxiliary buffers, and motion vectors. |
| `DeferredComposite.hlsl` | Final combine hosted by `DrawWorld::DeferredComposite` | Reads `kGbufferAlbedo=22`, `kDiffuseBuffer=58`, and `kSpecularBuffer=59`; writes `kMain=3`. |
| `VolumetricLighting.hlsl` | VLS slice-scatter pass hosted by `ImageSpaceEffectVLSLight::Render` | Reads linear depth at `t7`; writes one slice-accumulation target through `SV_Target0`. |
| `BSSkyShader.hlsl` | `BSSkyShader` pixel and vertex permutations | Nine pixel and seven vertex routes selected by exact SHA-1. |
| `BSWaterShader.hlsl` | `BSWaterShader` pixel and vertex permutations | Thirty-eight pixel and sixteen vertex routes selected by exact SHA-1. |
| `BSLightingShader.hlsl` | `BSLightingShader` pixel and vertex permutations | Twelve pixel and eight vertex routes selected by exact SHA-1. |

## Permutations

The runtime compiles a registered shader with a concrete set of preprocessor
defines. A named block define selects the implementation, and native defines
select its permutation.

Normal HLSL preprocessing removes inactive `#ifdef` branches before compilation,
so unused permutation branches add no runtime shader cost.

## Local verification

From the repository root, run:

```powershell
ctest --test-dir build -C Release -R "ShaderCompile|SharedDataDeclaration"
```

`ShaderCompile` compiles every registered shipping permutation, explicit
feature-composition, standalone-source, all 111 vertex permutations, and the
shared substrate in active and inactive modes through `D3DCompile`.
`SharedDataDeclaration` parses `Common/SharedData.hlsli` and enforces the outer
gate and its exact b5/b6-only resource footprint. Keep both green when editing
any file in this directory.

## Disk shader cache

Injected shaders compile through the caching policy, which keeps validated DXBC
under `Data/ShaderCache/FO4CommunityShaders/content-v1/<stage>/<xx>/<digest>.fxc`.
Nothing else in `Data/ShaderCache` is ever touched or removed.

A record is only reused when the whole compile still matches:

- The filename is the SHA-256 of the logical recipe — source locator, ordered
  include roots, ordered defines, entry point, profile, stage, compile flags and
  the SHA-256 of the loaded `d3dcompiler` module. Changing any of them addresses
  a different record, and the record repeats those digests so a swapped or
  hand-copied file is rejected rather than trusted.
- Each record carries the resolution trace of the compile that produced it: the
  root source digest, and for every `#include` each candidate path tried in
  order with its outcome. On lookup every entry is replayed. An edited source or
  header, an unreadable dependency, or a **new file appearing at a
  higher-priority candidate path** is a miss, so a shadowing header cannot be
  silently ignored.
- Corrupt, truncated, over-large or future-version records fall through to a
  normal compile. Failed compiles are never written.

Records are published by writing a uniquely named temp file next to the target
and renaming it over it, so a concurrent reader or a second game process never
sees a partial record. A publication failure only costs the next run a recompile;
it never changes the shader that was just compiled.

Each startup revalidates against one snapshot. The caching policy owns a
revalidation context for the duration of a freeze batch, so every unique source
and include path is read and hashed exactly once no matter how many recipes name
it, and every record in that batch is judged against the same observation.
Freshness is decided by content SHA-256 alone — never by timestamp or by size on
its own — and a length only ever travels with the digest it belongs to. Code that
passes no context keeps reading each dependency afresh on every lookup.

### Stale entries

There is no startup sweep in v1, by choice. A record for a currently disabled
feature is still a valid record: turning the feature back on should hit the cache
rather than pay for a recompile, and a sweep would delete exactly those entries.
Entries are content-addressed, so an orphan left behind by an edited shader costs
disk space and nothing else — it can never be mistaken for a live record. Users
who want that space back may delete `Data\ShaderCache\FO4CommunityShaders`, and
only that directory; everything in it is rebuilt on the next run.

Editing HLSL therefore needs no cache cleaning. `ctest -R ShaderCache` covers the
behaviour above. `ShaderCacheBenchmarkTests` is a separate executable, run by
`ctest -R ShaderCacheBenchmark`: it compiles every shipping registration with the
plain `CompileShaderToBlob` oracle, then cold, then warm, and requires all three
to be byte-identical before reporting the wall time each path took.

## Delivery caveat

Reconstruction is not delivery. A shader executes only when the engine draws
through a permutation registered by the plugin. Shader replacement is also
opt-in: `[shader_ownership]` is disabled by default in
`package/F4SE/Plugins/FO4CommunityShaders/FO4CommunityShaders.toml`.

A shader can therefore compile and ship without being exercised at runtime.

The replacement broker supports pixel and vertex stages. Vertex routes are
hash-only, and each device hook installs only when its stage is requested.
