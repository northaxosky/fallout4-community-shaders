#Requires -Version 5.1
<#
.SYNOPSIS
    Stage proprietary third-party SDK runtime DLLs (NVIDIA Streamline, AMD FidelityFX,
    Intel XeSS) into package\F4SE\Plugins.

.DESCRIPTION
    These runtime binaries are not build outputs and are intentionally not committed.
    Run once after cloning, or when SDK versions change. Archive-sourced SDKs are
    downloaded, SHA256-verified, and cached under .sdk-cache\; the XeSS DLLs are copied
    out of the extern\XeSS submodule. Versions, URLs, and checksums live in
    scripts\sdk-manifest.psd1.

.PARAMETER Force
    Re-download / re-copy even when the target DLLs are already present.

.PARAMETER Manifest
    Override the manifest path (defaults to scripts\sdk-manifest.psd1).

.EXAMPLE
    pwsh scripts\fetch-sdks.ps1

.EXAMPLE
    pwsh scripts\fetch-sdks.ps1 -Force
#>
[CmdletBinding()]
param(
    [switch]$Force,
    [string]$Manifest
)

$ErrorActionPreference = 'Stop'
# System.IO.Compression.FileSystem is loaded by default on PS 7 but not on 5.1.
try { Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop } catch { }

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot   = Split-Path -Parent $scriptDir
$packageDir = Join-Path $repoRoot 'package'
$cacheDir   = Join-Path $repoRoot '.sdk-cache'
if (-not $Manifest) { $Manifest = Join-Path $scriptDir 'sdk-manifest.psd1' }

if (-not (Test-Path $Manifest)) { throw "SDK manifest not found: $Manifest" }
$sdk = Import-PowerShellDataFile -Path $Manifest
New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null

function Test-Sha256 {
    param([string]$Path, [string]$Expected)
    $actual = (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
    if ($actual -ne $Expected.ToLowerInvariant()) {
        Remove-Item -Force -Path $Path -ErrorAction SilentlyContinue
        throw "SHA256 mismatch for $Path`n  expected: $($Expected.ToLowerInvariant())`n  actual:   $actual"
    }
}

function Get-Archive {
    param([string]$Name, [string]$Url, [string]$Sha256, [string]$OutFile)
    if (-not (Test-Path $OutFile)) {
        Write-Host "  Downloading $Name..."
        & curl.exe -fSL $Url -o $OutFile
        if ($LASTEXITCODE -ne 0) { throw "Failed to download $Url" }
    }
    Test-Sha256 -Path $OutFile -Expected $Sha256
}

function Expand-ArchiveEntries {
    param([string]$Zip, [string]$EntryDir, [string[]]$Dlls, [string]$Dest)
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        foreach ($dll in $Dlls) {
            $entryName = "$EntryDir/$dll"
            $entry = $archive.GetEntry($entryName)
            if (-not $entry) { Write-Warning "  $entryName not found in archive"; continue }
            $target = Join-Path $Dest $dll
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally {
        $archive.Dispose()
    }
}

function Test-AllPresent {
    param([string]$Dest, [string[]]$Dlls)
    foreach ($dll in $Dlls) {
        if (-not (Test-Path (Join-Path $Dest $dll))) { return $false }
    }
    return $true
}

Write-Host '=== Fetching SDK runtime DLLs ==='

foreach ($a in $sdk.Archives) {
    $dest = Join-Path $packageDir $a.Dest
    if ((Test-AllPresent -Dest $dest -Dlls $a.Dlls) -and (-not $Force)) {
        Write-Host "  $($a.Name) DLLs already present, skipping (use -Force to re-download)"
        continue
    }
    $zip = Join-Path $cacheDir $a.Archive
    Get-Archive -Name "$($a.Name) SDK $($a.Version)" -Url $a.Url -Sha256 $a.Sha256 -OutFile $zip
    Expand-ArchiveEntries -Zip $zip -EntryDir $a.EntryDir -Dlls $a.Dlls -Dest $dest
    if (-not (Test-Path (Join-Path $dest $a.Sentinel))) {
        throw "$($a.Sentinel) not found after extraction"
    }
    Write-Host "  $($a.Name) $($a.Version): $($a.Dlls.Count) DLLs staged"
}

foreach ($c in $sdk.Copies) {
    $dest = Join-Path $packageDir $c.Dest
    if ((Test-AllPresent -Dest $dest -Dlls $c.Dlls) -and (-not $Force)) {
        Write-Host "  $($c.Name) DLLs already present, skipping (use -Force to re-copy)"
        continue
    }
    $src = Join-Path $repoRoot $c.SourceDir
    if (-not (Test-Path $src)) {
        throw "$($c.Name) source not found: $src. Run: git submodule update --init $($c.Submodule)"
    }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    foreach ($dll in $c.Dlls) {
        $from = Join-Path $src $dll
        if (-not (Test-Path $from)) { throw "Missing $from" }
        Copy-Item -Force -Path $from -Destination (Join-Path $dest $dll)
    }
    Write-Host "  $($c.Name): $($c.Dlls.Count) DLLs copied from submodule"
}

Write-Host '=== Done ==='
