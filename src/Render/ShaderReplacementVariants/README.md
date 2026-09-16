# Shader replacement variants

These TOML files contain stock shader routes measured from Fallout 4. Each route maps a stock
SHA-1, source, and define set to a reconstructed shader target. A route can omit `source` to use
the target's default source. The hashes are inputs from the shipping executable; they cannot be
derived from the reconstructed shader sources.

CMake embeds the files in generated C++ sources under the build tree. The plugin has no runtime
dependency on the TOML files. File order is registration order. Target metadata also supplies the
canonical ownership configuration keys used by startup and the in-game menu.

Pixel replacement contributors bind at the matching draw anchor. Tiled-lighting compute
replacements are matched by their published shader identity at the stable tail of the engine's
`Renderer::RunComputeShader` helper, after its native shader and b0 setup. Contributed shared data
is bound immediately around the helper's direct `ID3D11DeviceContext::Dispatch`, with only CS
b5-b6 preserved and restored. Native SDK or third-party dispatches outside that engine
helper, including `DispatchIndirect`, are intentionally untouched. Contributor callbacks are
filtered by their registered shader stages. Baseline-only compute replacements do not activate
the shared-data scope.
