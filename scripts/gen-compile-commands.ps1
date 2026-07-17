#Requires -Version 5.1
<#
.SYNOPSIS
    Generate build/clangd/compile_commands.json for clangd-based editors (Zed, VS Code, neovim).

.DESCRIPTION
    The Visual Studio CMake generator cannot export a compile database, so this configures the
    repo with Ninja via the `clangd` CMake preset into build/clangd/. The committed .clangd file
    points clangd at that path; clangd auto-reloads it, so no editor restart is needed.

    Rerun this when you add a source file, feature, or dependency, or change compile flags in a
    CMakeLists.txt. Editing existing files needs no rerun.

    Runnable from a plain shell: it locates Visual Studio via vswhere, bootstraps the x64
    toolchain (cl + ninja) through vcvars64, and preserves VCPKG_ROOT (a VS developer prompt
    otherwise repoints it at the bundled vcpkg).

.PARAMETER VcpkgRoot
    Path to your vcpkg checkout. Defaults to the VCPKG_ROOT environment variable.

.EXAMPLE
    pwsh scripts\gen-compile-commands.ps1
#>
[CmdletBinding()]
param([string]$VcpkgRoot = $env:VCPKG_ROOT)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $VcpkgRoot) {
    throw "VCPKG_ROOT is not set. Set it to your vcpkg checkout, or pass -VcpkgRoot <path>."
}
if (-not (Test-Path (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))) {
    throw "No vcpkg toolchain under '$VcpkgRoot'. Point -VcpkgRoot at a valid vcpkg checkout."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found; is Visual Studio installed? Or run from a VS x64 developer prompt."
}
$vsInstall = & $vswhere -latest -products * -property installationPath | Select-Object -First 1
if (-not $vsInstall) { throw "No Visual Studio installation found via vswhere." }

$vcvars = Join-Path $vsInstall 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

# Prefer the ninja bundled with VS; fall back to whatever vcvars puts on PATH.
$ninja = Join-Path $vsInstall 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$makeProgramArg = if (Test-Path $ninja) { " -DCMAKE_MAKE_PROGRAM=`"$ninja`"" } else { '' }

# One cmd session: vcvars adds cl (+ninja); restore VCPKG_ROOT (vcvars clobbers it); configure.
$configure = "call `"$vcvars`" && set `"VCPKG_ROOT=$VcpkgRoot`" && cd /d `"$repoRoot`" && cmake --preset=clangd$makeProgramArg"

Write-Host "Generating compile_commands.json (Ninja, preset=clangd) -> build\clangd ..." -ForegroundColor Cyan
& $env:ComSpec /c $configure
if ($LASTEXITCODE -ne 0) { throw "cmake --preset=clangd failed (exit $LASTEXITCODE)." }

$db = Join-Path $repoRoot 'build\clangd\compile_commands.json'
if (-not (Test-Path $db)) { throw "Configure completed but $db was not produced." }

Write-Host "OK: $db" -ForegroundColor Green
Write-Host "clangd reloads it automatically; no editor restart needed." -ForegroundColor Green
