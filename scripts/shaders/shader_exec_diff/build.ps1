[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere.exe not found: $vswhere"
}

$installationPath = (& $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1)
if (-not $installationPath) {
    throw "No Visual Studio installation with the C++ x64 toolchain was found."
}

$vcvars64 = Join-Path $installationPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path -LiteralPath $vcvars64 -PathType Leaf)) {
    throw "vcvars64.bat not found: $vcvars64"
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$cacheDir = Join-Path $repoRoot ".shader-cache"
$source = Join-Path $PSScriptRoot "main.cpp"
$executable = Join-Path $cacheDir "shader_exec_diff.exe"
$object = Join-Path $cacheDir "shader_exec_diff.obj"
$compilerPdb = Join-Path $cacheDir "shader_exec_diff-compiler.pdb"
$linkerPdb = Join-Path $cacheDir "shader_exec_diff.pdb"
$vswhereDir = Split-Path -Parent $vswhere

New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null

$command = @(
    "set `"PATH=$vswhereDir;%PATH%`"",
    "&&",
    "call `"$vcvars64`" >nul",
    "&&",
    "cl /nologo /std:c++17 /O2 /EHsc /W4 /WX",
    "`"$source`"",
    "/Fo`"$object`"",
    "/Fd`"$compilerPdb`"",
    "/Fe`"$executable`"",
    "/link d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib",
    "/PDB:`"$linkerPdb`""
) -join " "

& $env:ComSpec /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "shader_exec_diff build failed with exit code $LASTEXITCODE."
}

Write-Host "Built $executable"
