<#
.SYNOPSIS
    Fail-closed check that the reconstructed FILTER axis still compiles to the
    native instruction stream.

.DESCRIPTION
    Compiles every permutation in scripts/shaders/filter-axis-native-shex.json,
    extracts the SHEX chunk of the resulting DXBC container - the instruction
    stream plus the immediate constant buffer, i.e. everything except the
    identifier-bearing reflection and signature chunks - and compares its
    SHA-256 against the same chunk taken from the archive blob.

    The pinned hashes come from the game's own bytecode, so this cannot bless
    its own output: an edit that changes codegen fails until it is reverted or
    until a new archive measurement justifies a new hash. Unlike
    verify-shader-roundtrip.ps1 this is not a producer attestation and carries
    no source pin; the SHEX hash is the pin.

.PARAMETER FxcPath
    Override fxc.exe. FXC_PATH is used when this parameter is omitted.

.PARAMETER ManifestPath
    Override the evidence file.

.PARAMETER RepoRoot
    Override the repository root used to resolve the shader source.
#>
[CmdletBinding()]
param(
    [string]$FxcPath,
    [string]$ManifestPath,
    [string]$RepoRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $RepoRoot) {
    $RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
}
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $PSScriptRoot 'filter-axis-native-shex.json'
}
if (-not $FxcPath) {
    $FxcPath = if ($env:FXC_PATH) {
        $env:FXC_PATH
    } else {
        'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe'
    }
}

function Exit-WithError([string]$Message, [int]$Code = 1) {
    Write-Host "FAIL: $Message"
    exit $Code
}

# DXBC container: 'DXBC', 16-byte digest, version, total size, chunk count,
# then one 32-bit offset per chunk; each chunk is a four-character code, a
# 32-bit payload size and the payload.
function Get-ShexChunk([byte[]]$Bytes) {
    if ($Bytes.Length -lt 32 -or [Text.Encoding]::ASCII.GetString($Bytes, 0, 4) -cne 'DXBC') {
        throw 'not a DXBC container'
    }
    $count = [BitConverter]::ToUInt32($Bytes, 28)
    for ($i = 0; $i -lt $count; ++$i) {
        $offset = [BitConverter]::ToUInt32($Bytes, 32 + ($i * 4))
        $fourcc = [Text.Encoding]::ASCII.GetString($Bytes, $offset, 4)
        if ($fourcc -ceq 'SHEX' -or $fourcc -ceq 'SHDR') {
            $size = [BitConverter]::ToUInt32($Bytes, $offset + 4)
            $payload = New-Object byte[] $size
            [Array]::Copy($Bytes, $offset + 8, $payload, 0, $size)
            return $payload
        }
    }
    throw 'no SHEX/SHDR chunk'
}

if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    Exit-WithError "evidence file not found: $ManifestPath" 2
}

try {
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
} catch {
    Exit-WithError "evidence file is not valid JSON: $($_.Exception.Message)" 2
}

if ($manifest.schema -cne 'fo4cs.native-shex-equality' -or $manifest.schema_version -ne 1) {
    Exit-WithError 'unexpected evidence schema' 2
}

$fxcCommand = $null
if (Test-Path -LiteralPath $FxcPath -PathType Leaf) {
    $fxcCommand = (Resolve-Path -LiteralPath $FxcPath).Path
} else {
    $command = Get-Command $FxcPath -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { $fxcCommand = $command.Source }
}
if (-not $fxcCommand) {
    Exit-WithError "fxc not found: $FxcPath" 2
}

$sourcePath = [IO.Path]::GetFullPath(
    (Join-Path $RepoRoot ($manifest.source.Replace('/', [IO.Path]::DirectorySeparatorChar))))
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    Exit-WithError "shader source not found: $sourcePath" 2
}
$includePath = Split-Path -Parent $sourcePath

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("fo4cs-filter-axis-" + [Guid]::NewGuid().ToString('n'))
$failures = @()
$checked = 0
try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    foreach ($entry in $manifest.entries) {
        $outputPath = Join-Path $tempRoot ($entry.target + '.dxbc')
        $arguments = @('/T', $manifest.profile, '/E', $manifest.compiler.entry_point, '/nologo', '/O3', '/I', $includePath)
        foreach ($define in @($manifest.common_defines) + @($entry.defines)) {
            $arguments += @('/D', $define)
        }
        $arguments += @('/Fo', $outputPath, $sourcePath)

        $compilerOutput = $null
        $compileExit = 1
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $compilerOutput = & $fxcCommand @arguments 2>&1
            $compileExit = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousPreference
        }

        ++$checked
        if ($compileExit -ne 0 -or -not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
            $failures += "$($entry.target): compile failed`n$(($compilerOutput | Out-String).Trim())"
            continue
        }

        try {
            $shex = Get-ShexChunk ([IO.File]::ReadAllBytes($outputPath))
        } catch {
            $failures += "$($entry.target): $($_.Exception.Message)"
            continue
        }

        if ($shex.Length -ne $entry.native_shex_bytes) {
            $failures += "$($entry.target): SHEX is $($shex.Length) bytes, native is $($entry.native_shex_bytes)"
            continue
        }

        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            $actual = ([BitConverter]::ToString($sha.ComputeHash($shex)) -replace '-', '').ToLowerInvariant()
        } finally {
            $sha.Dispose()
        }

        if ($actual -cne $entry.native_shex_sha256) {
            $failures += "$($entry.target): SHEX sha256 $actual, native $($entry.native_shex_sha256)"
        }
    }
} finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($checked -ne $manifest.entries.Count -or $checked -eq 0) {
    Exit-WithError "checked $checked of $($manifest.entries.Count) permutations"
}

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) { Write-Host "ERROR: $failure" }
    Exit-WithError "$($failures.Count) of $checked permutation(s) diverged from the native instruction stream"
}

Write-Host "PASS: $checked / $checked FILTER permutations match the native SHEX chunk."
exit 0
