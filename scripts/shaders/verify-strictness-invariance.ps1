# Verifies that the runtime's D3DCOMPILE_ENABLE_STRICTNESS flag (fxc /Ges) cannot
# change generated code. The runtime compiles with strictness; the producer attests
# without it, so shipped bytes never equal attested bytes. That is only acceptable
# while the difference is confined to RDEF, the reflection chunk that records
# compile flags. Every executable chunk must be byte-identical.
#
# Exits 2 when fxc is unavailable. It never skips.

[CmdletBinding()]
param(
    [string]$FxcPath,
    [string]$ManifestPath,
    [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'

if (-not $RepoRoot) {
    $RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $PSScriptRoot 'shader-fidelity-conformance.json'
}

function Resolve-Fxc {
    param([string]$Explicit)
    if ($Explicit -and (Test-Path -LiteralPath $Explicit)) { return $Explicit }
    if ($env:FXC_PATH -and (Test-Path -LiteralPath $env:FXC_PATH)) { return $env:FXC_PATH }
    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "${env:ProgramFiles}\Windows Kits\10\bin"
    )
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $found = Get-ChildItem -LiteralPath $root -Filter 'fxc.exe' -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

# DXBC: 'DXBC', 16-byte digest, version, total size, chunk count, then chunk offsets.
function Get-DxbcChunks {
    param([string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 32 -or
        [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -cne 'DXBC') {
        throw "not a DXBC container: $Path"
    }
    $count = [BitConverter]::ToUInt32($bytes, 28)
    $chunks = [ordered]@{}
    for ($i = 0; $i -lt $count; $i++) {
        $offset = [BitConverter]::ToUInt32($bytes, 32 + ($i * 4))
        $fourcc = [Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $size = [BitConverter]::ToUInt32($bytes, $offset + 4)
        $data = New-Object byte[] $size
        [Array]::Copy($bytes, $offset + 8, $data, 0, $size)
        $sha = [BitConverter]::ToString(
            [Security.Cryptography.SHA256]::Create().ComputeHash($data)
        ).Replace('-', '').ToLowerInvariant()
        $chunks[$fourcc] = $sha
    }
    return $chunks
}

$fxc = Resolve-Fxc -Explicit $FxcPath
if (-not $fxc) {
    Write-Host 'ERROR: fxc.exe not found; set FXC_PATH or pass -FxcPath.'
    exit 2
}
if (-not (Test-Path -LiteralPath $ManifestPath)) {
    Write-Host "ERROR: manifest not found: $ManifestPath"
    exit 1
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$entries = @($manifest.entries)
if ($entries.Count -eq 0) {
    Write-Host 'ERROR: manifest declares no entries.'
    exit 1
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('strictness-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null

# RDEF records the compile flags, so it is expected to differ. Everything else
# describes the shader itself and must not move.
$reflectionChunk = 'RDEF'
$errors = @()
$compared = 0

try {
    foreach ($entry in $entries) {
        $source = Join-Path $RepoRoot ($entry.source -replace '/', '\')
        if (-not (Test-Path -LiteralPath $source)) {
            $errors += "$($entry.target): source not found: $($entry.source)"
            continue
        }
        $includeRoot = Split-Path -Parent $source

        $variants = @{}
        foreach ($mode in @('attested', 'runtime')) {
            $output = Join-Path $scratch ("$($entry.target).$mode.dxbc")
            $arguments = @(
                '/T', $entry.profile,
                '/E', $manifest.compiler.entry_point,
                '/nologo', '/O3'
            )
            if ($mode -eq 'runtime') { $arguments += '/Ges' }
            $arguments += @('/I', $includeRoot)
            foreach ($define in @($entry.defines)) { $arguments += @('/D', $define) }
            $arguments += @('/Fo', $output, $source)

            & $fxc @arguments 2>&1 | Out-Null
            if (-not (Test-Path -LiteralPath $output)) {
                $errors += "$($entry.target): $mode compile failed"
                $variants = $null
                break
            }
            $variants[$mode] = Get-DxbcChunks -Path $output
        }
        if (-not $variants) { continue }

        $names = @($variants['attested'].Keys) + @($variants['runtime'].Keys) |
            Sort-Object -Unique
        foreach ($name in $names) {
            if ($name -ceq $reflectionChunk) { continue }
            $left = $variants['attested'][$name]
            $right = $variants['runtime'][$name]
            if ($left -cne $right) {
                $errors += "$($entry.target): $name differs under strictness ($left vs $right)"
            }
        }
        $compared++
    }
} finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

if ($errors.Count -gt 0) {
    foreach ($message in $errors) { Write-Host "ERROR: $message" }
    Write-Host "FAIL: strictness changed generated code in $($errors.Count) case(s)."
    exit 1
}

Write-Host ("PASS: {0} / {0} shaders keep every non-{1} chunk byte-identical under strictness." -f $compared, $reflectionChunk)
exit 0
