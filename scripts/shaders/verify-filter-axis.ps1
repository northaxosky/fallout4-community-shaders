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

    A manifest may additionally opt in to `container_identity`, which compares
    the whole DXBC container rather than the SHEX chunk alone. Without it a
    signature-only change - an unused interpolant's semantic index, say - leaves
    the instruction stream byte-identical and passes while the container no
    longer matches the blob. The pin is all-or-none across a manifest and
    implies /Qstrip_reflect, because native blobs are stripped. A manifest that
    omits the block behaves exactly as it did before the block existed.

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

function Get-Sha256Hex([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Bytes)) -replace '-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
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

# Optional container-identity pin. Absent, this behaves exactly as it always
# has and compares the SHEX chunk alone. Present, the whole DXBC container must
# equal the archive blob, which closes the gap where a signature-only change -
# an unused interpolant's semantic index, say - leaves the instruction stream
# byte-identical and the container different.
$pinContainer = $false
$containerPin = $manifest.PSObject.Properties['container_identity']
if ($containerPin) {
    if (-not $containerPin.Value.PSObject.Properties['enabled']) {
        Exit-WithError 'container_identity is present but declares no enabled flag' 2
    }
    $pinContainer = [bool]$containerPin.Value.enabled
}
# All-or-none: a family either pins every row's container or none of them, so
# the one row whose container would have differed cannot be quietly dropped.
if ($pinContainer) {
    $unpinned = @($manifest.entries | Where-Object {
        -not $_.PSObject.Properties['native_blob_sha256'] -or
        -not $_.PSObject.Properties['native_blob_bytes']
    }).Count
    if ($unpinned -gt 0) {
        Exit-WithError ("container_identity is enabled but $unpinned of " +
            "$($manifest.entries.Count) entries carry no native_blob_sha256/native_blob_bytes; " +
            'the pin is all-or-none') 2
    }
}

# Native blobs are stripped, so a container comparison only means anything
# against a stripped compile. This is an implication of the pin rather than a
# choice, which is why there is no separate toggle for it.
$compilerFlags = @('/nologo', '/O3')
if ($pinContainer) { $compilerFlags += '/Qstrip_reflect' }

# The flags the manifest advertises must be the flags this script runs.
# Recording them without checking them lets the two drift silently, so this is
# an enforced invariant rather than documentation.
if ((@($manifest.compiler.flags) -join ' ') -cne ($compilerFlags -join ' ')) {
    Exit-WithError ("compiler.flags does not match the flags this verifier runs" +
        "`n    manifest: $(@($manifest.compiler.flags) -join ' ')" +
        "`n    verifier: $($compilerFlags -join ' ')") 2
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
$containerChecked = 0
$shexDiverged = 0
$containerDiverged = 0
try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    foreach ($entry in $manifest.entries) {
        $outputPath = Join-Path $tempRoot ($entry.target + '.dxbc')
        $arguments = @('/T', $manifest.profile, '/E', $manifest.compiler.entry_point) + $compilerFlags + @('/I', $includePath)
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
            if ($pinContainer) { ++$containerChecked }
            continue
        }

        $container = [IO.File]::ReadAllBytes($outputPath)
        try {
            $shex = Get-ShexChunk $container
        } catch {
            $failures += "$($entry.target): $($_.Exception.Message)"
            if ($pinContainer) { ++$containerChecked }
            continue
        }

        $shexOk = $true
        if ($shex.Length -ne $entry.native_shex_bytes) {
            $failures += "$($entry.target): SHEX is $($shex.Length) bytes, native is $($entry.native_shex_bytes)"
            $shexOk = $false
        } else {
            $actual = Get-Sha256Hex $shex
            if ($actual -cne $entry.native_shex_sha256) {
                $failures += "$($entry.target): SHEX sha256 $actual, native $($entry.native_shex_sha256)"
                $shexOk = $false
            }
        }
        if (-not $shexOk) { ++$shexDiverged }

        # A container mismatch under a matching SHEX is a different defect from
        # a body mismatch, and says so: the instruction stream is right and a
        # signature or metadata chunk is wrong. Collapsing the two would throw
        # away the diagnostic this pin exists to buy.
        if ($pinContainer) {
            ++$containerChecked
            $containerActual = Get-Sha256Hex $container
            if ($container.Length -ne $entry.native_blob_bytes -or
                $containerActual -cne $entry.native_blob_sha256) {
                if ($shexOk) {
                    ++$containerDiverged
                    $failures += ("$($entry.target): matches the native instruction stream but " +
                        'diverged from the native container (signature or metadata chunk); ' +
                        "container is $($container.Length) bytes sha256 $containerActual, " +
                        "native is $($entry.native_blob_bytes) bytes sha256 $($entry.native_blob_sha256)")
                } else {
                    $failures += ("$($entry.target): container is $($container.Length) bytes " +
                        "sha256 $containerActual, native is $($entry.native_blob_bytes) bytes " +
                        "sha256 $($entry.native_blob_sha256)")
                }
            }
        }
    }
} finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($checked -ne $manifest.entries.Count -or $checked -eq 0) {
    Exit-WithError "checked $checked of $($manifest.entries.Count) permutations"
}

if ($pinContainer -and $containerChecked -ne $checked) {
    Exit-WithError "container pin missing: checked $containerChecked of $checked containers"
}

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) { Write-Host "ERROR: $failure" }
    $summary = @()
    if ($shexDiverged -gt 0) {
        $summary += "$shexDiverged of $checked permutation(s) diverged from the native instruction stream"
    }
    if ($containerDiverged -gt 0) {
        $summary += ("$containerDiverged of $checked permutation(s) matched the native instruction " +
            'stream but diverged from the native container (signature or metadata chunk)')
    }
    if ($summary.Count -eq 0) {
        $summary += "$($failures.Count) of $checked permutation(s) failed"
    }
    Exit-WithError ($summary -join '; ')
}

if ($pinContainer) {
    Write-Host ("PASS: $checked / $checked FILTER permutations match the native SHEX chunk " +
        "and $containerChecked / $checked the whole native container.")
} else {
    Write-Host "PASS: $checked / $checked FILTER permutations match the native SHEX chunk."
}
exit 0
