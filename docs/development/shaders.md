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

The declaration-derived census of the six reconstructed engine shader families
leaves only `b3`-`b8` and `b11`, textures from `t16`, and no sampler slots for
the plugin. `b5` and `b6` are reserved: a contributor that claims either as a
constant buffer quarantines its target.

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
gate and the declared Fallout 4 register budget. Keep both green when editing
any file in this directory.

## Delivery caveat

Reconstruction is not delivery. A shader executes only when the engine draws
through a permutation registered by the plugin. Shader replacement is also
opt-in: `[shader_ownership]` is disabled by default in
`package/F4SE/Plugins/FO4CommunityShaders/FO4CommunityShaders.toml`.

A shader can therefore compile and ship without being exercised at runtime.

The replacement broker supports pixel and vertex stages. Vertex routes are
hash-only, and each device hook installs only when its stage is requested.
