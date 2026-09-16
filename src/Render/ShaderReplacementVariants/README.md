# Shader replacement selection

Runtime selection uses the native family, stage, and resolved shader ID, following the upstream
family/descriptor model at `aebf01c2efa0a1926d676dc66d2623aaf58cc866`. The existing shader cache
prepares variants lazily. There is no runtime recipe catalogue or stock-hash admission.

ImageSpace uses its native source prefix and `GetImagespaceFXMacros` (virtual slot 17).
PrePass uses normalized stage IDs; `TEXTURE` comes from bit `0x2`, while early-depth comes from
the native bytecode. `Engine.h` isolates the AE/NG `BSShader` layout: VS/PS/CS maps at
`0x98/0x128/0x158` and `fxpFilename` at `0x188`, verified from constructor ID `2318862`.

| AE 1.11.240 boundary | Address Library ID |
|---|---:|
| `BSShader::BeginTechnique` | 2318876 |
| `BSShader::ReloadShaders(BSIStream*)` | 2318873 |
| `Renderer::SetShaders` | 2276942 |
| `Renderer::RunComputeShader` | 2276940 |
| `BSComputeShader::ReloadShaders(BSIStream*)` | 2319682 |

Graphics and compute substitution use metadata-preserving wrappers at the native setter
boundaries, preserving the engine's shader cache and constant tables. HS/DS remain native.
The compute dispatch tail scopes only shared `b5-b6` bindings and stage-filtered contributor
callbacks; baseline-only compute does not activate that scope.

`ShaderCompile` verifies the generated descriptors against the pinned AE 1.11.240 bytecode
fingerprints. Those fingerprints are offline proof evidence only.
