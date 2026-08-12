# Deferred shader reconstructions

`package/Shaders/` holds reconstructed HLSL for Fallout 4's deferred renderer.
`src/Render/ShaderInjection.cpp` compiles registered permutations at runtime and
injects them in place of the corresponding stock shaders.

These files are live product content. Changes affect shaders distributed with
the plugin; they are not reference copies.

## Layout

Each engine shader class has one public `.hlsl` entry point. Shader classes with
multiple source families keep their implementations in a directory with the
same name:

- `BSDFLight.hlsl` selects modules from `BSDFLight/`.
- `BSDFComposite.hlsl` selects modules from `BSDFComposite/`.
- `Common/` contains helpers shared by more than one entry point or family.

Feature shaders live in `features/<Name>/Shaders/<Name>/` and deploy alongside
these into `Data/Shaders/<Name>/`.

The two modular entry points require an explicit selector:

- `BSDF_LIGHT_FAMILY` selects one of the nine BSDFLight modules.
- `BSDF_COMPOSITE_FAMILY` selects one of the BSDFComposite modules.

The selector is not inferred from native feature macros. Those macros describe
features within a source family and do not form a unique routing key for every
family, so inference could silently choose the wrong implementation.

Each BSDFLight family module also checks its required and forbidden native
macros with `#error` guards. The nine modules contain 94 such guards. These
checks reject a family selector paired with the wrong native macro set.

## Entry points

| Entry point | Engine shader or pass | Principal bindings |
|---|---|---|
| `BSDFLight.hlsl` | `BSDFLightShader` deferred lighting, hosted by `DrawWorld::DeferredLightsImpl` and the directional-light accumulation path | Reads the lighting G-buffer aliases and main depth; writes `kDiffuseBuffer=58` and `kSpecularBuffer=59`. |
| `BSDFComposite.hlsl` | `BSDFCompositeShader`; the registered ambient and image-based lighting family runs within `DrawWorld::DeferredLightsImpl` | Bindings vary by family; the registered ambient family reads the G-buffer and `kSSAO=28`, then writes `kDiffuseBuffer=58`. |
| `BSDFPrePass.hlsl` | G-buffer fill hosted by `DrawWorld::DeferredPrePass` | Writes six MRT outputs: albedo, octahedral normal, material data, two auxiliary buffers, and motion vectors. |
| `DeferredComposite.hlsl` | Final combine hosted by `DrawWorld::DeferredComposite` | Reads `kGbufferAlbedo=22`, `kDiffuseBuffer=58`, and `kSpecularBuffer=59`; writes `kMain=3`. |
| `VolumetricLighting.hlsl` | VLS slice-scatter pass hosted by `ImageSpaceEffectVLSLight::Render` | Reads linear depth at `t7`; writes one slice-accumulation target through `SV_Target0`. |

## Permutations

The runtime compiles a registered shader with a concrete set of preprocessor
defines. The family define selects a module, and the remaining native defines
select the permutation within that module.

Normal HLSL preprocessing removes inactive `#ifdef` branches before compilation,
so unused permutation branches add no runtime shader cost.

## Local verification

From the repository root, run:

```powershell
ctest --test-dir build -C Release -R ShaderCompile
```

`ShaderCompile` compiles all 16 shipping lighting permutations through
`D3DCompile`, the same compiler used by runtime shader compilation. Keep this
test green when editing any file in this directory.

## Delivery caveat

Reconstruction is not delivery. A shader executes only when the engine draws
through a permutation registered by the plugin. Shader replacement is also
opt-in: `[shader_ownership]` is disabled by default in
`package/F4SE/Plugins/FO4CommunityShaders/FO4CommunityShaders.toml`.

A shader can therefore compile and ship without being exercised at runtime.
