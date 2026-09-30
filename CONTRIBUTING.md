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

## Upstream sync

Fetch in `extern\community-shaders-shared`, merge the desired upstream tag/SHA into its
`main`, and push that shared repository. Bump the FO4 CS submodule pin and validate the
DLL build, all tests, formatting, and package. Review changes to consumed manifest paths with
`git -C extern\community-shaders-shared log <old>..<new> -- <paths>`.
FO4 never replaces an upstream shader path: consume unchanged files through `xmake\shared.lua`;
host translations belong under `Shaders\FO4\` or in a minimal shared seam commit.

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

## Format

```powershell
pwsh scripts\format.ps1
```

CI runs `scripts\format.ps1 -Check` on C++ and HLSL. Run
`git config blame.ignoreRevsFile .git-blame-ignore-revs` once to skip the bulk reformat in blame.

## Changelog

`CHANGELOG.md` is the only maintained release history. It feeds the in-game
**General > Changelog** page and stable release notes. Keep versions newest first,
using only the `# Changelog` title, `## MAJOR.MINOR.PATCH` headings, single-line
`- ` bullets, and blank lines. Builds embed the file directly in the DLL.

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

Launch through MO2/F4SE. Enable only the features being tested in `Data\F4SE\Plugins\FO4CommunityShaders\FO4CommunityShaders.toml`, created on first launch.
