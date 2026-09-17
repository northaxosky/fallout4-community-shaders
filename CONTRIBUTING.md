# Contributing

FO4 Community Shaders is an experimental Windows plugin built as one F4SE DLL. Keep
changes focused, preserve support for the declared runtime, and treat successful
compilation as separate from in-game rendering validation.

## Requirements

The default local toolchain is:

- Windows x64
- Visual Studio 2026 with the Desktop development with C++ workload
- [xmake](https://xmake.io)
- CMake, used by xmake only to build the FidelityFX SDK submodule
- [Git](https://git-scm.com/) and [Git LFS](https://git-lfs.com/)

PowerShell 7 (`pwsh`) is required for Streamline's vendor-provided trust-header
generator and the SDK staging script.

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

Configure a release-with-debug-information build, then build the plugin:

```bash
xmake f -m releasedbg -y
xmake
```

Use `-m release` for an optimized build without debug information. The plugin is
written below `build\windows\x64\<mode>\`. Release configurations use link-time
optimization and treat project compiler and linker warnings as errors. Git
identifies development builds.

For clangd-based editors, `pwsh scripts\gen-compile-commands.ps1` generates the compile database.

Community Shaders does not compile or link Dear ImGui. Its UI uses the official forwarding-only
DearModdingUI headers published through the pinned CommonLibF4 submodule at
`extern\CommonLibF4\lib\dearmoddingui-api\include`. Update that CommonLibF4 gitlink when adopting a
new API release; do not vendor a second API copy or add a local ImGui port.

Use the shared `dmui::DrawStyledText`, `SettingsTableScope`, `SettingsRowScope`,
`TooltipScope`, and keyed `DrawChoice` helpers for presentation. Keep stable
choice values and IDs separate from visible labels, and apply changes only when
the helper reports a different selection. Scope failures are distinct from
clipping; report failures through the existing host diagnostics. Feature state,
restart requirements, preset transactions, and persistence remain owned by
Community Shaders rather than by these helpers.

The packaged unified TOML sets every `[features.<Name>].load = false` and baseline shader ownership
to disabled. Override only the feature being tested, or `enabled` under `[shader_ownership]` for an
identity replacement test, in `FO4CommunityShaders.User.toml`, then restart Fallout 4. The core
D3D11 bootstrap remains available when every feature is inactive. Settings UI requires a compatible
DearModdingUI host; without one the plugin deliberately runs headless.

## Test

Build the non-default C++ test executables and run all 37 xmake-native
registrations:

```bash
xmake test
```

`ShaderCompile` stages the merged shipping shader tree and compiles every
permutation of the reconstructed deferred shaders through `D3DCompile`, the
same compiler the plugin uses at runtime. Editing those shaders must keep it
green.

The GPU interop test defaults to WARP. Run
`build\windows\x64\releasedbg\FrameGenerationRetirementGpuTests.exe --hardware`
to check bidirectional shared-texture handoff and retirement on a hardware adapter.

## Install or deploy

After building, create the CommonLibF4 package with:

```bash
xmake package FO4CommunityShaders
```

The archive is written to `build\packages\` and contains the plugin,
configuration, SDK runtimes, and merged feature shader layout under `Data\`.

To deploy with CommonLibF4, set one of these environment variables before
running `xmake install`:

- `XSE_FO4_MODS_PATH` installs to
  `%XSE_FO4_MODS_PATH%\FO4CommunityShaders`.
- `XSE_FO4_GAME_PATH` installs to `%XSE_FO4_GAME_PATH%\Data`.

```bash
xmake install
```

Launch the result through MO2/F4SE. Building and deploying does not perform any
visual comparison; rendering behavior must be checked in game.

When Tracy support is enabled, the renderer-owned profiler marks the completed deferred-composite
engine frame. It does not claim generated frame-generation presents or every swap-chain present.

## Project layout

```text
src\                Private source/headers, renderer hooks, forwarded UI, and presets
src\Host\           Forwarding-only DearModdingUI client integration
features\<Name>\    Feature source and optional runtime-compiled shaders
extern\             Third-party submodules, headers, and libraries
package\            Mod assets: config, presets, reconstructed shaders
scripts\            Developer tooling
tests\              Host and shader tests run by xmake test
xmake\              Focused xmake package, SDK, and shader-staging modules
```

Before submitting a change, build the affected configuration, run `xmake test`, and perform
an in-game check for rendering or hook changes.
