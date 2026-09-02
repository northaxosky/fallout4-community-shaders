# Shader replacement variants

These TOML files contain stock shader routes measured from Fallout 4. Each route maps a stock
SHA-1 and define set to a reconstructed shader target. The hashes are inputs from the shipping
executable; they cannot be derived from the reconstructed shader sources.

CMake embeds the files in generated C++ sources under the build tree. The plugin has no runtime
dependency on the TOML files. File order is registration order.
