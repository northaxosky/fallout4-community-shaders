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
for the compiled target, including a contribution that only binds a resource.
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

Upstream Common shaders use `t17` and `t20`; `t18`-`t23` remain reserved
headroom. Plugin feature textures begin at `t24`: ScreenSpaceShadows uses
`t24`, WetnessEffects uses `t25`, and ScreenSpaceGI uses `t26`-`t29`.

A slot claim is admitted once, at registration: the injection service keeps a
per-target ledger of claimed slots and contributed define values, and
`RegisterReplacement` rejects a substrate reservation, a second claim on the
same slot, or a conflicting define before it commits the registration. First
claimant wins in feature-registration order. The freeze pass repeats those
checks only as an invariant screen; a contributor that trips one there is
dropped and the target records a collision.

A contribution that carries a bind callback also needs the deferred draw
anchor. `RegisterReplacement` installs and verifies that anchor before it
commits, so a rejected anchor leaves no registration and no claim behind.

## Feature composition headers

A feature that composes into a reconstruction owns the HLSL for it. The header
lives beside the feature's own shaders and is included only inside the family
blocks that consume it, never globally.

`ScreenSpaceGI/ScreenSpaceGI.hlsli` is included by `BSDFCompositeShader.hlsl`
under `SSGI` in the three ambient families that can isolate directional ambient:
`CB31` (10 rows), `CB47` (8 rows) and `COMPACT` (8 rows). `MINIMAL` folds ambient
into its base color and is excluded, as are the non-ambient families. The header
declares one contiguous run of archive-free pixel slots, which the feature's
injection contribution binds in a single call on every injected draw:

| Slot | Contents |
|---|---|
| `t26` | occlusion, `R8_UNORM`, 0 open and 1 occluded |
| `t27` | indirect SH-L1 luma, `R16G16B16A16_FLOAT` |
| `t28` | indirect CoCg chroma, `R16G16_FLOAT` |
| `t29` | `RenderTarget::kGbufferAlbedo` |

`SharedData::screenSpaceGISettings.EnableScreenSpaceGI` gates the composition at
runtime; at zero every targeted row keeps the stock engine ambient-occlusion
path, so the injected variant is safe to leave installed.

`WetnessEffects/WetnessEffects.hlsli` is included by `BSDFLightShader.hlsl` and
`BSDFCompositeShader.hlsl` under `WETNESS_EFFECTS`. It owns the rain-facing
wetness scalar, the wet-albedo transform, the per-light GGX water-film coat and
the environment-BRDF film weight. `SharedData::wetnessEffectsSettings.Wetness`
gates all of it: at zero every helper returns its input unchanged, so an
injected variant is output-equivalent to the dry path at runtime.

Only the composite consumer declares a resource. The seven composite families
that participate define `WETNESS_COMPOSITE_CONSUMER` before the include, which
declares `Texture2D<float4> GbufferNormal : register(t25)`; BSDFLight includes
the same header without that define and binds no texture. The feature rebinds
`RenderTarget::kGbufferNormal` at `t25` on every injected composite draw,
because `kGbufferNormalSwap` can move the authoritative target, and binds null
explicitly when it cannot resolve one. A null or otherwise out-of-domain load
fails the encode-domain guard and yields wetness identity for that draw.

Wetness routes by role, not by material ID:

| Target rows | Wetness behavior |
|---|---|
| 146 analytical BSDFLight rows | per-light Fresnel attenuation plus one water-film GGX lobe |
| 3 in-family `ATTENUATION_ONLY` rows | untouched; they carry transport, not shaded material |
| 30 composite rows with a base multiply | wet albedo before the base multiply |
| 40 composite rows with a native reflection lobe | `K * lerp(Ldry, Lwet, Fenv) * P` inside that lobe |
| 26 SSGI composition rows | the same wet albedo on SSGI's own `t29` albedo |

BSDFLight rows read world-up from `SharedData::WorldUpView` (b5, offset 112);
every composite family uses its own native view-to-world row 2 instead. The
feature's telemetry publishes the b5 value per frame as `world_up_view_*` so a
capture can compare it against the native row for the same frame.

## Deferred radiance source

ScreenSpaceGI generates at `PostDeferredLightsImpl`, which runs after
`DrawWorld::DeferredLightsImpl` has written the deferred lighting buffers and
before `DrawWorld::DeferredComposite` consumes them.

A direct copy-probe in one live tiled-on RenderDoc capture pins the RT-pool
slots to their engine resources:

| Pool slot | Engine resource | Role |
|---|---|---|
| 58 (`kDiffuseBufferA`) | Resource759 | DiffuseA, final DirectDiffuse |
| 59 (`kProbeBufferA`) | Resource762 | ProbeA |
| 60 (`kDiffuseBufferB`) | Resource765 | DiffuseB, final DirectSpecular |
| 61 (`kProbeBufferB`) | Resource769 | ProbeB |

The tiled compute event writes Resource765 and Resource769 and republishes them
through pool slots 60 and 61. **Those two slots are originally created as
unrelated 876x700 Pip-Boy targets and are only repointed while tiled lighting is
active**, so reading them unconditionally samples the Pip-Boy. The feature only
retrieves the B SRV when the runtime tiled-lighting predicate
(`cs::engine::QueryTiledLightingEnabled`) is present *and* true, and then only
uses it when its descriptor matches A on width, height, `R11G11B10_FLOAT` and
single-sample. The predicate is `std::nullopt` on OG, where the getter has no
resolved address; that path is A-only. NG and AE use Address Library ID
`2318371`; the official 1.11.240 database resolves it to RVA `0x21FA9A0`.

The radiance the sweep gathers is therefore `RT58 * 3` with tiled lighting off
and `(RT58 + RT60) * 3` with it on. RT59 and RT61 are excluded. The scalar is
applied to the raw float values: no transfer-function conversion and no albedo
multiplication happen at the source.

Source contamination is bounded and documented rather than stripped: 16 of 167
`BSDFLight` classes and 24 of 307 routes, all `DIRSPLITS2` rows carrying
`AMBIENT_IBL_IN_LIGHT`, already fold ambient image-based lighting into RT58.
Feature telemetry reports those counts alongside the live source count.

Motion vectors come from `RenderTarget::kMotionVectors=29` (`R16G16_FLOAT`,
full resolution, single sample). `BSDFPrePass` writes `(currNDC - prevNDC)`
scaled by `(-0.5, +0.5)`, so reprojection is `previousUV = currentUV + motion`.
RT29 is supplied by the native deferred-prepass route or the owned
`BSDFPrePass` reconstruction when shader ownership is enabled; no compatibility
feature is required.

The pass order is radiance capture and history reprojection, radiance mips,
depth and normal mips, GI generation and temporal SH/CoCg blend, then spatial
denoise and publication. AO stays spatial-only. History resets on allocation
changes, source/input changes, re-enable, temporal-setting changes, loading
screen close, missing motion or inputs, frame gaps, and camera discontinuities.
Previous geometry uses its own projection and the renderer position adjustment,
so world-origin rebases remain coherent.

## Entry points

| Entry point | Engine shader or pass | Principal bindings |
|---|---|---|
| `BSDFLightShader.hlsl` | `BSDFLightShader` deferred lighting, hosted by `DrawWorld::DeferredLightsImpl` and the directional-light accumulation path | Reads the lighting G-buffer aliases and main depth; writes `kDiffuseBufferA=58` and `kProbeBufferA=59`. |
| `BSDFCompositeShader.hlsl` | `BSDFCompositeShader`; ambient, image-based lighting, and composite families | Bindings and outputs vary by selected family. |
| `BSDFPrePass.hlsl` | G-buffer fill hosted by `DrawWorld::DeferredPrePass` | Writes six MRT outputs: albedo, octahedral normal, material data, two auxiliary buffers, and motion vectors. |
| `DeferredComposite.hlsl` | Final combine hosted by `DrawWorld::DeferredComposite` | Reads `kGbufferAlbedo=22`, `kDiffuseBufferA=58`, and `kProbeBufferA=59`; writes `kMain=3`. |
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
shared substrate in active and inactive modes through `D3DCompile`. It also
witnesses reflection: `t26`-`t29` must appear on all 26 composing
`kBsdfComposite` pixel rows built with `SSGI` and on none of the other 44, and
all eight ScreenSpaceGI permutations that use `XeGTAOCB` must reflect the same
288-byte layout at the same offsets, so a conditional field cannot silently
shift the buffer between the AO and bounce builds.
`SharedDataDeclaration` parses `Common/SharedData.hlsli` and enforces the outer
gate and its exact b5/b6-only resource footprint. Keep both green when editing
any file in this directory.

## Disk shader cache

Injected shaders compile through the caching policy, which keeps validated DXBC
under `Data/ShaderCache/FO4CommunityShaders/content-v1/<stage>/<xx>/<digest>.fxc`.
Nothing else in `Data/ShaderCache` is ever touched or removed.

A record is only reused when the whole compile still matches:

- The filename is the SHA-256 of the logical recipe: source locator, ordered
  include roots, ordered defines, entry point, profile, stage, compile flags and
  the SHA-256 of the loaded `d3dcompiler` module. Changing any of them addresses
  a different record, and the record repeats those digests so a swapped or
  hand-copied file is rejected rather than trusted.
- Each record carries the resolution trace of the compile that produced it: the
  root source digest, and for every `#include` each candidate path tried in
  order with its outcome. On lookup every entry is replayed. An edited source or
  header, an unreadable dependency, or a **new file appearing at a
  higher-priority candidate path** is a miss, so a shadowing header cannot be
  silently ignored. Recorded candidate paths are canonical, so this replay
  assumes the filesystem mapping remains stable.
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
Freshness is decided by content SHA-256 alone, never by timestamp or by size on
its own, and a length only ever travels with the digest it belongs to. Code that
passes no context keeps reading each dependency afresh on every lookup.

### Stale entries

There is no startup sweep in v1, by choice. A record for a currently disabled
feature is still a valid record: turning the feature back on should hit the cache
rather than pay for a recompile, and a sweep would delete exactly those entries.
Entries are content-addressed, so an orphan left behind by an edited shader costs
disk space and nothing else; it can never be mistaken for a live record. Users
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
