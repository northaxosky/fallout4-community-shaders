# Contributing

Keep changes focused and preserve existing behavior. Rendering changes need in-game
validation as well as a successful build.

## Requirements

- Windows x64
- Visual Studio 2026 with the Desktop development with C++ workload
- [xmake](https://xmake.io)
- [Git](https://git-scm.com/) and [Git LFS](https://git-lfs.com/)
- PowerShell 7 (`pwsh`)

## Clone

```powershell
git lfs install
git clone --recursive https://github.com/northaxosky/fallout4-community-shaders.git
cd fallout4-community-shaders
git lfs pull
git submodule foreach --recursive "git lfs install --local && git lfs pull"
```

## Stage SDK runtimes

SDK runtime binaries are staged, not committed. Run the staging script after cloning,
whenever `scripts\sdk-manifest.psd1` changes, and to pick up a newer Streamline build:

```powershell
pwsh scripts\fetch-sdks.ps1
```

Streamline comes from the latest `northaxosky/Streamline` release, which that fork
publishes on every `main` push. `extern\Streamline` supplies only the headers.

## Configure and build

Builds may install automatically. Use an isolated destination for development:

```powershell
Remove-Item Env:\XSE_FO4_MODS_PATH, Env:\XSE_FO4_GAME_PATH -ErrorAction SilentlyContinue
$env:INSTALLDIR = Join-Path $PWD 'build\isolated-install'
xmake f -m releasedbg -y
xmake
```

The DLL is written to `build\windows\x64\releasedbg\`.
For clangd, generate a compile database with `pwsh scripts\gen-compile-commands.ps1`.

## Test

```powershell
xmake test
```

Add regression tests for critical behavior and real failure modes, not implementation
details. Validate rendering and hook changes in game before claiming they work.

## Install or deploy

```powershell
xmake package FO4CommunityShaders
```

Packages are written to `build\packages\`.

To deploy, set one destination before running `xmake install`:

- `XSE_FO4_MODS_PATH` installs to
  `%XSE_FO4_MODS_PATH%\FO4CommunityShaders`.
- `XSE_FO4_GAME_PATH` installs to `%XSE_FO4_GAME_PATH%\Data`.

```powershell
xmake install
```

Launch through MO2/F4SE. Enable only the features being tested in `Data\F4SE\Plugins\FO4CommunityShaders\FO4CommunityShaders.toml`, filled in on first launch.
