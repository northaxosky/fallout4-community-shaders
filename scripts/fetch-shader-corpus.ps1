#requires -Version 5.0
<#
.SYNOPSIS
    Extract the original FO4 deferred-pipeline shader blobs needed by
    shader_corpus_diff.py out of the game's shader archive.

.DESCRIPTION
    shader_corpus_diff.py compares our reconstructed HLSL against the real game
    ASM. The game bytes are Bethesda's and must never be committed to this public
    repo, so they are fetched on demand into a gitignored .corpus/ directory,
    the same pattern fetch-sdks.sh uses for proprietary SDK DLLs.

    Steps: locate 'Fallout4 - Shaders.ba2' (BTDX/GNRL), pull the single contained
    'Shaders011.fxp', scan it for embedded DXBC blobs, and for every shader in the
    manifest that carries a corpus_sha1 write the matching blob to
    .corpus/<name>.dxbc. Nothing is written for shaders without a corpus_sha1.

.PARAMETER GamePath
    Fallout 4 install root (the folder containing Data\). Auto-detected from a few
    common Steam locations and the FALLOUT4_PATH env var when omitted.

.PARAMETER Ba2Path
    Explicit path to 'Fallout4 - Shaders.ba2', overriding GamePath discovery.

.PARAMETER OutDir
    Destination for extracted blobs. Default: <repo>\.corpus (gitignored).

.PARAMETER Manifest
    Shader manifest. Default: scripts\shader-roundtrip-baselines.json.

.EXAMPLE
    pwsh scripts/fetch-shader-corpus.ps1
    pwsh scripts/fetch-shader-corpus.ps1 -GamePath "D:\SteamLibrary\steamapps\common\Fallout 4"
#>

[CmdletBinding()]
param(
    [string]$GamePath,
    [string]$Ba2Path,
    [string]$OutDir,
    [string]$Manifest
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent $scriptDir
if (-not $OutDir)   { $OutDir   = Join-Path $repoRoot ".corpus" }
if (-not $Manifest) { $Manifest = Join-Path $scriptDir "shader-roundtrip-baselines.json" }

function Resolve-Ba2Path {
    if ($Ba2Path) {
        if (-not (Test-Path $Ba2Path)) { throw "Ba2Path not found: $Ba2Path" }
        return (Resolve-Path $Ba2Path).Path
    }
    $roots = @()
    if ($GamePath)            { $roots += $GamePath }
    if ($env:FALLOUT4_PATH)   { $roots += $env:FALLOUT4_PATH }
    $roots += @(
        "C:\Games\Steam\steamapps\common\Fallout 4",
        "C:\Program Files (x86)\Steam\steamapps\common\Fallout 4",
        "D:\SteamLibrary\steamapps\common\Fallout 4",
        "E:\SteamLibrary\steamapps\common\Fallout 4"
    )
    foreach ($r in $roots) {
        $cand = Join-Path $r "Data\Fallout4 - Shaders.ba2"
        if (Test-Path $cand) { return (Resolve-Path $cand).Path }
    }
    throw "Could not find 'Fallout4 - Shaders.ba2'. Pass -GamePath <install root> or -Ba2Path <file>."
}

# --- Extract Shaders011.fxp region from the BTDX/GNRL archive ---
function Get-FxpBytes([string]$ba2) {
    $bytes = [System.IO.File]::ReadAllBytes($ba2)
    $magic = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4)
    $type  = [System.Text.Encoding]::ASCII.GetString($bytes, 8, 4)
    if ($magic -ne "BTDX") { throw "Not a BTDX archive: $ba2" }
    if ($type  -ne "GNRL") { throw "Unsupported archive type '$type' (expected GNRL)." }
    $numFiles = [System.BitConverter]::ToUInt32($bytes, 12)
    if ($numFiles -lt 1) { throw "Archive reports 0 files." }
    # First GNRL record starts at offset 24; layout:
    #   nameHash u32, ext 4b, dirHash u32, flags u32, offset u64, packed u32, unpacked u32, align u32
    $recBase = 24
    $offset   = [System.BitConverter]::ToUInt64($bytes, $recBase + 16)
    $packed   = [System.BitConverter]::ToUInt32($bytes, $recBase + 24)
    $unpacked = [System.BitConverter]::ToUInt32($bytes, $recBase + 28)
    if ($packed -ne 0) {
        throw "The shader .fxp is zlib-compressed in this archive; this fetcher only handles the stored (uncompressed) form."
    }
    $fxp = New-Object byte[] $unpacked
    [System.Array]::Copy($bytes, [int64]$offset, $fxp, 0, $unpacked)
    return ,$fxp
}

# --- Enumerate embedded DXBC blobs, keyed by sha1 ---
function Get-DxbcBlobs([byte[]]$fxp) {
    # Ordinal byte scan for the 'DXBC' magic; totalSize lives at magic+24.
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)  # ISO-8859-1, 1:1 byte<->char
    $s = $latin1.GetString($fxp)
    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    $blobs = [System.Collections.Generic.Dictionary[string,object]]::new()
    $index = 0
    $pos = 0
    while ($true) {
        $j = $s.IndexOf("DXBC", $pos, [System.StringComparison]::Ordinal)
        if ($j -lt 0) { break }
        if ($j + 32 -gt $fxp.Length) { break }
        $totalSize = [System.BitConverter]::ToUInt32($fxp, $j + 24)
        if ($totalSize -le 0 -or ($j + $totalSize) -gt $fxp.Length) { $pos = $j + 4; continue }
        $hash = ($sha1.ComputeHash($fxp, $j, [int]$totalSize) | ForEach-Object { $_.ToString("x2") }) -join ""
        if (-not $blobs.ContainsKey($hash)) {
            $blobs[$hash] = [pscustomobject]@{ Index = $index; Offset = $j; Size = [int]$totalSize }
        }
        $index++
        $pos = $j + [int]$totalSize
    }
    return @{ Bytes = $fxp; ByHash = $blobs; Count = $index }
}

Write-Host "Locating shader archive..." -ForegroundColor Cyan
$ba2 = Resolve-Ba2Path
Write-Host "  $ba2"

Write-Host "Extracting Shaders011.fxp and scanning DXBC blobs..." -ForegroundColor Cyan
$fxp = Get-FxpBytes $ba2
$scan = Get-DxbcBlobs $fxp
Write-Host ("  {0} unique DXBC blobs found." -f $scan.ByHash.Count)

$config = Get-Content $Manifest -Raw | ConvertFrom-Json
$wanted = @($config.shaders | Where-Object { $_.corpus_sha1 })
if ($wanted.Count -eq 0) { Write-Host "No manifest entries carry a corpus_sha1; nothing to fetch." -ForegroundColor Yellow; exit 0 }

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$missing = @()
foreach ($shader in $wanted) {
    $sha = ($shader.corpus_sha1).ToLowerInvariant()
    if ($scan.ByHash.ContainsKey($sha)) {
        $b = $scan.ByHash[$sha]
        $dst = Join-Path $OutDir ("{0}.dxbc" -f $shader.name)
        $slice = New-Object byte[] $b.Size
        [System.Array]::Copy($scan.Bytes, $b.Offset, $slice, 0, $b.Size)
        [System.IO.File]::WriteAllBytes($dst, $slice)
        Write-Host ("  [ok]   {0}  <- blob #{1} ({2} bytes)" -f $shader.name, $b.Index, $b.Size) -ForegroundColor Green
    } else {
        $missing += $shader.name
        Write-Host ("  [miss] {0}  (corpus_sha1 {1} not present in this archive)" -f $shader.name, $sha) -ForegroundColor Red
    }
}

Write-Host ""
Write-Host ("Corpus written to {0}" -f $OutDir)
if ($missing.Count -gt 0) {
    Write-Host ("WARNING: {0} shader(s) had no matching blob: {1}" -f $missing.Count, ($missing -join ", ")) -ForegroundColor Yellow
    Write-Host "This usually means the game version differs from the one the corpus_sha1 was recorded against."
    exit 1
}
exit 0
