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

- Windows PowerShell 5.1 or PowerShell 7 (`pwsh`), plus `curl` (bundled with Windows 10 1803+), for runtime SDK staging (`scripts/fetch-sdks.ps1`)

## Clone

Clone recursively because CommonLibF4, FidelityFX-SDK, Streamline, and XeSS are
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
git -C extern/Streamline lfs pull
```

The Streamline submodule contains LFS-backed NGX libraries required at link time.
CMake reports an actionable error if they are missing or remain as pointer files.

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

## Stage runtime SDKs

The source submodules provide headers and link libraries. A complete mod installation
also needs proprietary runtime DLLs, which are not build outputs:

```powershell
pwsh scripts\fetch-sdks.ps1
```

Run it with PowerShell 7 (`pwsh`) or Windows PowerShell 5.1
(`powershell.exe -File scripts\fetch-sdks.ps1`). It verifies and caches the NVIDIA
Streamline and AMD FidelityFX archives, then stages NVIDIA, AMD, and Intel runtime DLLs
under `package\F4SE\Plugins\`. Pass `-Force` to refresh files already present.

## Install or deploy

The repository does not have a CMake install target or package-generation target. For
a manual source installation, mirror `package\` into the mod's `Data\`, then add the
build output and feature shaders:

- `build\Release\FO4CommunityShaders.dll` (and `.pdb`) -> `Data\F4SE\Plugins\`
- `features\<Name>\Shaders\<Name>\` -> `Data\Shaders\<Name>\`

`package\` mirrors the mod's `Data\` directly: `package\Shaders\` holds the reconstructed
deferred shaders and `package\F4SE\` holds plugin configuration, presets, LUTs, and staged
runtime SDK DLLs.

Launch the result through MO2/F4SE. Building and deploying does not perform any
visual comparison; rendering behavior must be checked in game.

## Project layout

```text
src\                Core feature framework, renderer hooks, menu, and presets
features\<Name>\    Feature source and optional runtime-compiled shaders
cmake\              Build integration for CommonLibF4 and graphics SDKs
extern\             Recursive source submodules
package\            Mod assets: config, presets, reconstructed shaders, staged SDK files
scripts\            SDK staging and developer tooling
tests\              Host and shader tests run by CTest
```

Before submitting a change, build the affected configuration, run CTest, and perform
an in-game check for rendering or hook changes.
