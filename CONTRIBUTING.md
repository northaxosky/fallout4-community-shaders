# Contributing

FO4 Community Shaders is an experimental Windows plugin built as one F4SE DLL. Keep
changes focused, preserve support for the declared runtime, and treat successful
compilation as separate from in-game rendering validation.

## Requirements

The default local toolchain is:

- Windows x64
- Visual Studio 2026 with the Desktop development with C++ workload
- CMake 4.2 or newer
- [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set
- [Git](https://git-scm.com/) and [Git LFS](https://git-lfs.com/)

Visual Studio 2022 is also supported through the `ci` preset and requires CMake 3.21
or newer. The two presets share the `build` directory, so use a clean build directory
when switching generators.

CTest additionally requires PowerShell 7 (`pwsh`) and `fxc.exe` from the Windows SDK.
Set `FXC_PATH` to use a compiler outside the default Windows SDK location.

Optional tooling:

- Windows PowerShell 5.1 or PowerShell 7 (`pwsh`) for the developer scripts under `scripts\`

## Clone

Clone recursively because CommonLibF4, FidelityFX-SDK, and Streamline are
submodules, and CommonLibF4 has its own nested submodule.

```bash
git lfs install
git clone --recursive https://github.com/northaxosky/fallout4-community-shaders.git
cd fallout4-community-shaders
git lfs pull
git submodule foreach --recursive "git lfs install --local && git lfs pull"
```

If checkout stopped with `git-lfs: command not found`, install Git LFS, open a new
shell, and resume from the repository root:

```bash
git lfs install
git submodule sync --recursive
git submodule update --init --recursive --checkout
```

## Stage the SDK runtime DLLs

Streamline's interposer, DLSS plugin and `nvngx_dlss.dll` are proprietary and are not vendored.
Run the staging script once after cloning, and again whenever `scripts\sdk-manifest.psd1` changes:

```bash
pwsh scripts/fetch-sdks.ps1
```

It downloads each pinned archive, verifies its SHA-256 against the manifest, and stages the
required files into the `Streamline` and `FidelityFX` directories under
`features\Upscaling\Shaders\Upscaling\`. The FidelityFX DX11 backend builds from the
`extern\FidelityFX-SDK` submodule, while frame generation requires the two staged AMD DX12 DLLs.

## Configure and build

Visual Studio 2026:

```bash
cmake -S . --preset=default
cmake --build build --config Release --target FO4CommunityShaders --parallel
```

Visual Studio 2022:

```bash
cmake -S . --preset=ci
cmake --build build --config Release --target FO4CommunityShaders --parallel
```

The plugin is written to `build\Release\FO4CommunityShaders.dll`. Release builds use
link-time optimization and treat compiler and linker warnings as errors.

Dear ImGui comes from the overlay vcpkg port in `vcpkg\ports\imgui`, pinned to an exact upstream
commit rather than a version tag, and `vcpkg-configuration.json` points vcpkg at it. The optional
shared-menu host contract compares the compiled layout, so re-syncing the port and re-vendoring
`include\DearModdingUI\API.h` plus `ImGuiFingerprint.h` go together. Both directories carry the
sync procedure, and `src\Host\HostFingerprint.h` fails the build if they drift apart.

The packaged unified TOML sets every `[features.<Name>].load = false` and baseline shader ownership
to disabled. Override only the feature being tested, or `enabled` under `[shader_ownership]` for an
identity replacement test, in `FO4CommunityShaders.User.toml`, then restart Fallout 4. The core
D3D11 bootstrap and settings menu remain available when every feature is inactive.

## Test

Run the registered CTest suite against the Release build:

```bash
ctest --test-dir build -C Release --output-on-failure
```

`ShaderCompile` compiles every shipping permutation of the reconstructed deferred shaders under
`package\Shaders\` through `D3DCompile`, the same compiler the
plugin uses at runtime. Editing those shaders must keep it green.

## Install or deploy

The repository does not have a CMake install target or package-generation target. For
a manual source installation, mirror `package\` into the mod's `Data\`, then add the
build output and feature shaders:

- `build\Release\FO4CommunityShaders.dll` (and `.pdb`) -> `Data\F4SE\Plugins\`
- `features\<Name>\Shaders\<Name>\` -> `Data\Shaders\<Name>\`

`package\` mirrors the mod's `Data\` directly: `package\Shaders\` holds the reconstructed
deferred shaders and `package\F4SE\` holds plugin configuration and presets.

Launch the result through MO2/F4SE. Building and deploying does not perform any
visual comparison; rendering behavior must be checked in game.

## Project layout

```text
src\                Core feature framework, renderer hooks, menu, and presets
src\Host\           Optional shared mod-menu integration and standalone fallback
features\<Name>\    Feature source and optional runtime-compiled shaders
cmake\              Build integration for CommonLibF4
extern\             Recursive source submodules
include\            Shared headers and the vendored DearModdingUI client ABI
package\            Mod assets: config, presets, reconstructed shaders
scripts\            Developer tooling
tests\              Host and shader tests run by CTest
vcpkg\ports\        Overlay ports pinning dependencies vcpkg cannot pin exactly
docs\               Developer documentation
```

Before submitting a change, build the affected configuration, run CTest, and perform
an in-game check for rendering or hook changes.
