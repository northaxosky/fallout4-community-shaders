#Requires -Version 5.1
<#
.SYNOPSIS
    Hermetic unit tests for verify-filter-axis.ps1.

.DESCRIPTION
    Drives the verifier against synthetic DXBC containers produced by a fake
    compiler, so nothing here needs fxc, the archive blobs or the real shader
    sources. The fixtures exist to pin the container_identity contract: that a
    manifest without the block behaves exactly as it did before the block
    existed, that a signature-only divergence passes without the pin and fails
    with it, that the pin is all-or-none, and that compiler.flags must equal the
    flags the verifier actually runs.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$verifier = Join-Path $scriptDir 'verify-filter-axis.ps1'
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('filter-axis-tests-' + [Guid]::NewGuid().ToString('N'))
$repoRoot = Join-Path $fixtureRoot 'repo'
$manifestDir = Join-Path $fixtureRoot 'manifests'
$fakeFxc = Join-Path $fixtureRoot 'fake-fxc.cmd'
$hostExecutable = (Get-Process -Id $PID).Path
$script:passed = 0
$script:failed = 0

# Minimal but structurally real DXBC: 'DXBC', 16-byte digest, version, total
# size, chunk count, one offset per chunk, then each chunk's fourcc, size and
# payload. Two containers that share a SHEX payload and differ in ISGN are
# exactly the shape a signature-only source change produces.
function New-Container {
    param([byte[]]$Isgn, [byte[]]$Shex)
    $chunks = @(
        @{ fourcc = 'ISGN'; payload = $Isgn },
        @{ fourcc = 'SHEX'; payload = $Shex }
    )
    $headerSize = 32 + (4 * $chunks.Count)
    $stream = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter($stream)
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes('DXBC'))
        $writer.Write((New-Object byte[] 16))
        $writer.Write([uint32]1)
        $total = $headerSize
        foreach ($chunk in $chunks) { $total += 8 + $chunk.payload.Length }
        $writer.Write([uint32]$total)
        $writer.Write([uint32]$chunks.Count)
        $offset = $headerSize
        foreach ($chunk in $chunks) {
            $writer.Write([uint32]$offset)
            $offset += 8 + $chunk.payload.Length
        }
        foreach ($chunk in $chunks) {
            $writer.Write([Text.Encoding]::ASCII.GetBytes($chunk.fourcc))
            $writer.Write([uint32]$chunk.payload.Length)
            $writer.Write($chunk.payload)
        }
        $writer.Flush()
        return $stream.ToArray()
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

function Get-Sha256Hex([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Bytes)) -replace '-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function New-Manifest {
    param([switch]$PinContainer, [string[]]$Flags)
    if (-not $Flags) {
        $Flags = if ($PinContainer) { @('/nologo', '/O3', '/Qstrip_reflect') } else { @('/nologo', '/O3') }
    }
    $entries = @('pcf1', 'pcf9') | ForEach-Object {
        [pscustomobject][ordered]@{
            target             = $_
            defines            = @("FILTER_$($_.ToUpperInvariant())=1")
            native_blob_sha1   = ('a' * 40)
            native_blob_sha256 = $script:goodContainerSha256
            native_blob_bytes  = $script:goodContainer.Length
            native_shex_bytes  = $script:shexPayload.Length
            native_shex_sha256 = $script:shexSha256
        }
    }
    $manifest = [pscustomobject][ordered]@{
        schema         = 'fo4cs.native-shex-equality'
        schema_version = 1
        scope          = [pscustomobject][ordered]@{ family = 'fixture'; axis = 'FILTER'; runtime = 'fixture'; note = 'fixture' }
        compiler       = [pscustomobject][ordered]@{ name = 'fxc'; entry_point = 'main'; flags = @($Flags); include_root = 'source-directory' }
        common_defines = @('FIXTURE=1')
        source         = 'shaders/lighting/fixture.hlsl'
        profile        = 'ps_5_0'
        entries        = $entries
    }
    if ($PinContainer) {
        $manifest | Add-Member -NotePropertyName container_identity -NotePropertyValue ([pscustomobject]@{ enabled = $true })
    }
    return $manifest
}

function Write-Manifest {
    param([object]$Manifest, [string]$Name)
    $path = Join-Path $manifestDir ($Name + '.json')
    $Manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

function Invoke-Verifier {
    param([string]$ManifestPath, [string]$Emit = 'good')
    $old = $env:FAKE_FXC_EMIT
    try {
        $env:FAKE_FXC_EMIT = Join-Path $fixtureRoot "$Emit.dxbc"
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $output = & $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $verifier `
                -ManifestPath $ManifestPath -RepoRoot $repoRoot -FxcPath $fakeFxc 2>&1
        } finally {
            $ErrorActionPreference = $previous
        }
        return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output | Out-String) }
    } finally {
        $env:FAKE_FXC_EMIT = $old
    }
}

function Assert-Result {
    param([object]$Result, [int]$ExitCode, [string]$Pattern)
    if ($Result.ExitCode -ne $ExitCode) {
        throw "expected exit $ExitCode, got $($Result.ExitCode): $($Result.Output)"
    }
    if ($Pattern -and $Result.Output -notmatch $Pattern) {
        throw "output did not match '$Pattern': $($Result.Output)"
    }
}

function Invoke-Test {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body
        $script:passed++
    } catch {
        $script:failed++
        [Console]::Error.WriteLine("FAIL: ${Name}: $($_.Exception.Message)")
    }
}

try {
    New-Item -ItemType Directory -Path (Join-Path $repoRoot 'shaders\lighting') -Force | Out-Null
    New-Item -ItemType Directory -Path $manifestDir -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $repoRoot 'shaders\lighting\fixture.hlsl') `
        -Value 'fixture' -Encoding ASCII -NoNewline

    $script:shexPayload = [byte[]](1..64)
    $script:goodContainer = New-Container ([byte[]](200..231)) $script:shexPayload
    # Same instruction stream, different signature chunk: the whole point.
    $script:signatureOnlyContainer = New-Container ([byte[]](100..131)) $script:shexPayload
    $script:bodyContainer = New-Container ([byte[]](200..231)) ([byte[]](9..72))
    $script:goodContainerSha256 = Get-Sha256Hex $script:goodContainer
    $script:shexSha256 = Get-Sha256Hex $script:shexPayload

    [IO.File]::WriteAllBytes((Join-Path $fixtureRoot 'good.dxbc'), $script:goodContainer)
    [IO.File]::WriteAllBytes((Join-Path $fixtureRoot 'signature.dxbc'), $script:signatureOnlyContainer)
    [IO.File]::WriteAllBytes((Join-Path $fixtureRoot 'body.dxbc'), $script:bodyContainer)

    @'
@echo off
setlocal
set "out="
:args
if "%~1"=="" goto run
if /I "%~1"=="/Fo" goto output
shift
goto args
:output
shift
set "out=%~1"
shift
goto args
:run
copy /y "%FAKE_FXC_EMIT%" "%out%" >nul
exit /b 0
'@ | Set-Content -LiteralPath $fakeFxc -Encoding ASCII

    Invoke-Test 'pin absent: passes and reports SHEX only' {
        $result = Invoke-Verifier (Write-Manifest (New-Manifest) 'no-pin')
        Assert-Result $result 0 'PASS: 2 / 2 FILTER permutations match the native SHEX chunk\.'
        if ($result.Output -match 'container') { throw 'unpinned run mentioned the container' }
    }

    Invoke-Test 'pin enabled: passes and reports the container' {
        Assert-Result (Invoke-Verifier (Write-Manifest (New-Manifest -PinContainer) 'pinned')) 0 `
            'match the native SHEX chunk and 2 / 2 the whole native container\.'
    }

    # Scenario D: today's gate is green on a signature-only divergence.
    Invoke-Test 'signature-only divergence passes without the pin' {
        Assert-Result (Invoke-Verifier (Write-Manifest (New-Manifest) 'no-pin-signature') 'signature') 0 `
            'PASS: 2 / 2'
    }

    # Scenario E: the pin catches it, and says it is the signature, not the body.
    Invoke-Test 'signature-only divergence fails with the pin' {
        $result = Invoke-Verifier (Write-Manifest (New-Manifest -PinContainer) 'pinned-signature') 'signature'
        Assert-Result $result 1 'matched the native instruction stream but diverged from the native container'
        if ($result.Output -match 'diverged from the native instruction stream;') {
            throw 'a signature-only divergence was reported as an instruction-stream divergence'
        }
    }

    Invoke-Test 'body divergence still reports the instruction stream' {
        $result = Invoke-Verifier (Write-Manifest (New-Manifest -PinContainer) 'pinned-body') 'body'
        Assert-Result $result 1 'diverged from the native instruction stream'
        if ($result.Output -match 'matched the native instruction stream') {
            throw 'a body divergence was reported as a container-only divergence'
        }
    }

    # Scenario F: the pin cannot be applied to a subset.
    Invoke-Test 'pin is all-or-none' {
        $manifest = New-Manifest -PinContainer
        $manifest.entries[1].PSObject.Properties.Remove('native_blob_sha256')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'partial-pin')) 2 `
            'container_identity is enabled but 1 of 2 entries carry no native_blob_sha256'
    }

    Invoke-Test 'container_identity without enabled is refused' {
        $manifest = New-Manifest
        $manifest | Add-Member -NotePropertyName container_identity -NotePropertyValue ([pscustomobject]@{ note = 'x' })
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'pin-no-enabled')) 2 `
            'container_identity is present but declares no enabled flag'
    }

    Invoke-Test 'advertised compiler flags must match the ones run' {
        Assert-Result (Invoke-Verifier (Write-Manifest (New-Manifest -Flags @('/nologo', '/O2')) 'bad-flags')) 2 `
            'compiler\.flags does not match the flags this verifier runs'
    }

    Invoke-Test 'pinned manifest must advertise the strip flag' {
        $manifest = New-Manifest -PinContainer -Flags @('/nologo', '/O3')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'pinned-missing-strip')) 2 `
            'compiler\.flags does not match the flags this verifier runs'
    }

    Invoke-Test 'unpinned manifest must not advertise the strip flag' {
        $manifest = New-Manifest -Flags @('/nologo', '/O3', '/Qstrip_reflect')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'unpinned-extra-strip')) 2 `
            'compiler\.flags does not match the flags this verifier runs'
    }
} finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failed -gt 0) {
    [Console]::Error.WriteLine("FAIL: $($script:failed) failed, $($script:passed) passed.")
    exit 1
}
Write-Host "PASS: $($script:passed) tests."
exit 0
