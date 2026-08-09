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
- The shared devkit workbench for deploy, launch, and log-tail workflows

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

`ShaderRoundtrip` recompiles the reconstructed deferred shaders under
`shaders\lighting\` and checks each DXBC hash against
`scripts\shaders\shader-fidelity-conformance.json`, a **producer-published
attestation** copied byte-for-byte from the sibling `fallout4-re`. It is
fail-closed and there is no `-UpdateBaselines`: **the consumer may never
re-baseline it, and the manifest must never be hand-edited.** Refreshing it
requires an authoritative PASS in `fallout4-re`, its `fidelity
publish-conformance` step, and copying the artifact here unchanged.

Editing a pinned shader invalidates that entry's `source_sha256` and turns the
gate red before it compiles anything. That is by design, not drift — the fix is
a producer republication, never a local edit.

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
a manual source installation, mirror `package\F4SE\Plugins\` into the mod's
`Data\F4SE\Plugins\`, then add the build output and the two shader trees that live
outside `package\`:

- `build\Release\FO4CommunityShaders.dll` (and `.pdb`) -> `Data\F4SE\Plugins\`
- `features\Imagespace\Shaders\*.hlsl*` -> `Data\F4SE\Plugins\FO4CommunityShaders\Imagespace\Shaders\`
- `shaders\` (recursive) -> `Data\F4SE\Plugins\FO4CommunityShaders\Shaders\`

The shared devkit's `community-shaders.psd1` `Deploy` block encodes this same mapping
for automated deployment.

The optional shared devkit automates this workflow. Place it at `..\devkit\` (or set
`DEVKIT_DIR`) and drive it with PowerShell. A first deployment should include
configuration, shaders, presets, LUTs, and runtime SDK files:

```powershell
pwsh ..\devkit\devkit.ps1 deploy -Project community-shaders -IncludeConfig
```

Common iterative workflows:

```powershell
pwsh ..\devkit\devkit.ps1 cycle  -Project community-shaders                # build + deploy
pwsh ..\devkit\devkit.ps1 deploy -Project community-shaders                # deploy only
pwsh ..\devkit\devkit.ps1 cycle  -Project community-shaders -Launch -Tail  # build, deploy, launch, tail
```

The last command builds, deploys, launches Fallout 4 through MO2/F4SE, and tails the
plugin log. It does not perform automated visual comparison; rendering behavior must
be checked in game.

To diagnose a devkit setup directly:

```powershell
pwsh ..\devkit\devkit.ps1 doctor -Project community-shaders
```

## Project layout

```text
src\                Core feature framework, renderer hooks, menu, and presets
features\<Name>\    Feature source and optional runtime-compiled shaders
shaders\lighting\   Reconstructed deferred shaders, pinned by producer attestation
cmake\              Build integration for CommonLibF4 and graphics SDKs
extern\             Recursive source submodules
package\            Static mod assets and staged runtime SDK files
scripts\             SDK, deployment, and shader-validation tooling
```

Before submitting a change, build the affected configuration, run CTest, and perform
an in-game check for rendering or hook changes.
