# Shader replacement variants

These TOML files contain stock shader routes measured from Fallout 4. Each route maps a stock
SHA-1 and define set to a reconstructed shader target. The hashes are inputs from the shipping
executable; they cannot be derived from the reconstructed shader sources.

CMake embeds the files in generated C++ sources under the build tree. The plugin has no runtime
dependency on the TOML files. File order is registration order.

Pixel replacement contributors bind at the matching draw anchor. Tiled-lighting compute replacements are matched
by their published shader identity at `ID3D11DeviceContext::Dispatch` or `DispatchIndirect`;
contributed shared data is bound immediately around that dispatch, with only CS b5-b7 and t3
preserved and restored. Contributor callbacks are filtered by their registered shader stages.
Baseline-only compute replacements do not activate the shared-data scope.
