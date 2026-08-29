# imgui overlay port

Overlay copy of the upstream vcpkg `imgui` port, pinned to an exact Dear ImGui commit instead of a
released version tag.

| | |
|---|---|
| Upstream commit | `9acdfbf46810c0c74ab281ce04122c4149ae8bd1` |
| Version | `1.92.9b`, `IMGUI_VERSION_NUM 19291`, docking branch |
| Enabled features | `dx11-binding`, `win32-binding`, `docking-experimental` (see the root `vcpkg.json`) |

## Why it is pinned this way

The optional DearModdingUI host contract (`include/DearModdingUI/API.h`) refuses a client whose
Dear ImGui build does not match the host byte for byte. It compares the upstream commit string,
`IMGUI_VERSION_NUM`, the docking flag, and six `IMGUI_CHECKVERSION` type sizes. A version-only or
floating pin can silently drift onto a different docking-branch snapshot with the same version
number, so the port names the commit and its archive `SHA512` directly.

`src/Host/HostFingerprint.h` asserts the same commit and version at compile time, so a mismatch
between this port and the vendored header fails the build rather than a registration at runtime.

## Re-syncing

1. Copy `portfile.cmake`, `CMakeLists.txt`, `imgui-config.cmake.in`, and `vcpkg.json` from
   `$VCPKG_ROOT/ports/imgui`.
2. Reapply the pin: set `IMGUI_UPSTREAM_COMMIT` and the `SHA512` of
   `https://github.com/ocornut/imgui/archive/<commit>.tar.gz` in `portfile.cmake`, and set
   `version-string` in `vcpkg.json`.
3. Update `DMUI_IMGUI_UPSTREAM_COMMIT` expectations only by re-vendoring `API.h` from the host
   project; never edit the vendored header.

`vcpkg-configuration.json` at the repository root points vcpkg at `vcpkg/ports`.
