#Requires -Version 5.1
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$verifier = Join-Path $scriptDir 'verify-shader-roundtrip.ps1'
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('shader-roundtrip-tests-' + [Guid]::NewGuid().ToString('N'))
$repoRoot = Join-Path $fixtureRoot 'repo'
$manifestDir = Join-Path $fixtureRoot 'manifests'
$fakeFxc = Join-Path $fixtureRoot 'fake-fxc.cmd'
$marker = Join-Path $fixtureRoot 'fxc-invocations.txt'
$hostExecutable = (Get-Process -Id $PID).Path
$script:passed = 0
$script:failed = 0

function Get-LowerHash {
    param([string]$Path, [string]$Algorithm)

    $stream = [IO.File]::OpenRead($Path)
    $hasher = if ($Algorithm -eq 'SHA1') {
        [Security.Cryptography.SHA1]::Create()
    } else {
        [Security.Cryptography.SHA256]::Create()
    }
    try {
        return ([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $hasher.Dispose()
        $stream.Dispose()
    }
}

function New-Entry {
    param([string]$Target, [string]$Source, [string[]]$Defines)
    $sourcePath = Join-Path $repoRoot ($Source.Replace('/', [IO.Path]::DirectorySeparatorChar))
    return [pscustomobject][ordered]@{
        target = $Target
        source = $Source
        defines = @($Defines)
        profile = 'ps_5_0'
        source_sha256 = Get-LowerHash $sourcePath 'SHA256'
        expected_dxbc_sha1 = $script:compiledHash
    }
}

function New-ValidManifest {
    $entries = @(
        (New-Entry 'ambient_ibl_pass' 'shaders/lighting/ambient_ibl_pass.hlsl' @()),
        (New-Entry 'ambient_ibl_pass_runtime_no_tilelight' 'shaders/lighting/ambient_ibl_pass_runtime.hlsl' @()),
        (New-Entry 'ambient_ibl_pass_runtime' 'shaders/lighting/ambient_ibl_pass_runtime.hlsl' @('TILELIGHT=1')),
        (New-Entry 'bsdf_light_deferred_directional_ibl' 'shaders/lighting/bsdf_light_deferred.hlsl' @('AMBIENT_IBL_IN_LIGHT=1', 'LIGHT_TYPE=1')),
        (New-Entry 'bsdf_light_deferred_directional' 'shaders/lighting/bsdf_light_deferred.hlsl' @('LIGHT_TYPE=1')),
        (New-Entry 'bsdf_light_deferred_point' 'shaders/lighting/bsdf_light_deferred.hlsl' @('LIGHT_TYPE=2')),
        (New-Entry 'deferred_prepass' 'shaders/lighting/deferred_prepass.hlsl' @()),
        (New-Entry 'vls_slice_scatter' 'shaders/lighting/vls_slice_scatter.hlsl' @())
    )
    return [pscustomobject][ordered]@{
        schema = 'fo4re.shader-fidelity-conformance'
        schema_version = 1
        evidence = [pscustomobject][ordered]@{
            run_manifest_sha256 = ('1' * 64)
            contracts_sha256 = ('2' * 64)
            native_targets_sha256 = ('3' * 64)
            archive_sha256 = '4ac98b8fe72385f0cd13053bf5e81203fdaabe101377011e152457b705060659'
            archive_member_sha256 = 'f3254023504c4bab250162284a28efe2ed79fbf7a9b81e26d0fa9f22660d1d1f'
            harness_version = 4
        }
        compiler = [pscustomobject][ordered]@{
            name = 'fxc'
            entry_point = 'main'
            flags = @('/nologo', '/O3')
            include_root = 'source-directory'
        }
        entries = $entries
    }
}

function Write-Manifest {
    param([object]$Manifest, [string]$Name)
    $path = Join-Path $manifestDir ($Name + '.json')
    $Manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

function Add-RawJsonAfter {
    param([string]$Path, [string]$Pattern, [string]$Text)
    $json = Get-Content -LiteralPath $Path -Raw
    $match = [regex]::Match($json, $Pattern)
    if (-not $match.Success) { throw "fixture pattern not found: $Pattern" }
    $json = $json.Insert($match.Index + $match.Length, $Text)
    Set-Content -LiteralPath $Path -Value $json -Encoding UTF8 -NoNewline
}

function Invoke-Verifier {
    param(
        [string]$ManifestPath,
        [string]$CompilerPath = $fakeFxc,
        [switch]$CompilerFails
    )

    $oldFail = $env:FAKE_FXC_FAIL
    $oldMarker = $env:FAKE_FXC_MARKER
    try {
        $env:FAKE_FXC_FAIL = if ($CompilerFails) { '1' } else { $null }
        $env:FAKE_FXC_MARKER = $marker
        $oldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $output = & $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $verifier `
                -ManifestPath $ManifestPath -RepoRoot $repoRoot -FxcPath $CompilerPath 2>&1
        } finally {
            $ErrorActionPreference = $oldPreference
        }
        return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output | Out-String) }
    } finally {
        $env:FAKE_FXC_FAIL = $oldFail
        $env:FAKE_FXC_MARKER = $oldMarker
    }
}

function Assert-Result {
    param(
        [object]$Result,
        [int]$ExitCode,
        [string]$Pattern
    )
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
    $sources = @(
        'ambient_ibl_pass.hlsl',
        'ambient_ibl_pass_runtime.hlsl',
        'bsdf_light_deferred.hlsl',
        'deferred_prepass.hlsl',
        'vls_slice_scatter.hlsl',
        'alternate.hlsl',
        'extra.hlsl'
    )
    foreach ($source in $sources) {
        Set-Content -LiteralPath (Join-Path $repoRoot "shaders\lighting\$source") `
            -Value "fixture:$source" -Encoding ASCII -NoNewline
    }
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'compiled.bin') `
        -Value 'compiled' -Encoding ASCII -NoNewline
    $script:compiledHash = Get-LowerHash (Join-Path $fixtureRoot 'compiled.bin') 'SHA1'

    @'
@echo off
setlocal
if defined FAKE_FXC_MARKER echo %*>>"%FAKE_FXC_MARKER%"
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
if "%FAKE_FXC_FAIL%"=="1" exit /b 9
<nul set /p "=compiled" >"%out%"
exit /b 0
'@ | Set-Content -LiteralPath $fakeFxc -Encoding ASCII

    Invoke-Test 'valid pass' {
        Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
        $result = Invoke-Verifier (Write-Manifest (New-ValidManifest) 'valid')
        Assert-Result $result 0 'PASS: 8 / 8'
        $calls = @(Get-Content -LiteralPath $marker)
        if ($calls.Count -ne 8) { throw 'fxc was not invoked 8 times' }
        $includePath = [regex]::Escape(
            [IO.Path]::GetFullPath((Join-Path $repoRoot 'shaders\lighting')))
        if (@($calls | Where-Object { $_ -notmatch "/I `"?$includePath`"? (?:/D|/Fo) " }).Count -ne 0 -or
            $calls[0] -notmatch '^/T ps_5_0 /E main /nologo /O3 /I .+ /Fo ' -or
            $calls[2] -notmatch '/D TILELIGHT=1 /Fo ' -or
            $calls[3] -notmatch '/D AMBIENT_IBL_IN_LIGHT=1 /D LIGHT_TYPE=1 /Fo ') {
            throw 'fxc arguments did not match the manifest contract'
        }
    }

    Invoke-Test 'malformed JSON' {
        $path = Join-Path $manifestDir 'malformed.json'
        Set-Content -LiteralPath $path -Value '{"schema":' -Encoding ASCII
        Assert-Result (Invoke-Verifier $path) 1 'malformed manifest JSON'
    }

    Invoke-Test 'escaped duplicate top-level field' {
        $path = Write-Manifest (New-ValidManifest) 'duplicate-top'
        Add-RawJsonAfter $path '^\s*\{' '"\u0073chema":"invalid",'
        Assert-Result (Invoke-Verifier $path) 1 "duplicate JSON property 'schema'"
    }

    Invoke-Test 'duplicate evidence field' {
        $path = Write-Manifest (New-ValidManifest) 'duplicate-evidence'
        Add-RawJsonAfter $path '"evidence"\s*:\s*\{' '"run_manifest_sha256":"invalid",'
        Assert-Result (Invoke-Verifier $path) 1 "duplicate JSON property 'run_manifest_sha256'"
    }

    Invoke-Test 'duplicate compiler field' {
        $path = Write-Manifest (New-ValidManifest) 'duplicate-compiler'
        Add-RawJsonAfter $path '"compiler"\s*:\s*\{' '"name":"invalid",'
        Assert-Result (Invoke-Verifier $path) 1 "duplicate JSON property 'name'"
    }

    Invoke-Test 'duplicate entry field' {
        $path = Write-Manifest (New-ValidManifest) 'duplicate-entry-field'
        Add-RawJsonAfter $path '"entries"\s*:\s*\[\s*\{' '"target":"invalid",'
        Assert-Result (Invoke-Verifier $path) 1 "duplicate JSON property 'target'"
    }

    Invoke-Test 'object-like text in string' {
        $manifest = New-ValidManifest
        $manifest.schema = '{"schema":"fo4re.shader-fidelity-conformance","evidence":{"name":"fxc"}}'
        $result = Invoke-Verifier (Write-Manifest $manifest 'object-like-string')
        Assert-Result $result 1 'schema must be exactly'
        if ($result.Output -match 'duplicate JSON property') {
            throw 'scanner treated string contents as object structure'
        }
    }

    Invoke-Test 'wrong top-level field order' {
        $valid = New-ValidManifest
        $manifest = [pscustomobject][ordered]@{
            schema_version = $valid.schema_version
            schema = $valid.schema
            evidence = $valid.evidence
            compiler = $valid.compiler
            entries = $valid.entries
        }
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'field-order')) 1 'fields must be exactly, in order'
    }

    Invoke-Test 'wrong evidence field order' {
        $manifest = New-ValidManifest
        $valid = $manifest.evidence
        $manifest.evidence = [pscustomobject][ordered]@{
            contracts_sha256 = $valid.contracts_sha256
            run_manifest_sha256 = $valid.run_manifest_sha256
            native_targets_sha256 = $valid.native_targets_sha256
            archive_sha256 = $valid.archive_sha256
            archive_member_sha256 = $valid.archive_member_sha256
            harness_version = $valid.harness_version
        }
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'evidence-order')) 1 'evidence fields must be exactly'
    }

    Invoke-Test 'wrong schema' {
        $manifest = New-ValidManifest
        $manifest.schema = 'wrong'
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'schema')) 1 'schema must be exactly'
    }

    Invoke-Test 'wrong schema version' {
        $manifest = New-ValidManifest
        $path = Write-Manifest $manifest 'version'
        $json = (Get-Content -LiteralPath $path -Raw) -replace
            '("schema_version"\s*:\s*)1([,\r\n])', '${1}1.0$2'
        Set-Content -LiteralPath $path -Value $json -Encoding UTF8 -NoNewline
        Assert-Result (Invoke-Verifier $path) 1 'schema_version must be integer 1'
    }

    Invoke-Test 'unknown field' {
        $manifest = New-ValidManifest
        $manifest | Add-Member -NotePropertyName unknown -NotePropertyValue $true
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'unknown')) 1 'fields must be exactly'
    }

    Invoke-Test 'missing field' {
        $manifest = New-ValidManifest
        $manifest.PSObject.Properties.Remove('compiler')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'missing')) 1 'fields must be exactly'
    }

    Invoke-Test 'nested unknown and missing fields' {
        $manifest = New-ValidManifest
        $manifest.evidence | Add-Member -NotePropertyName unknown -NotePropertyValue $true
        $manifest.entries[0].PSObject.Properties.Remove('expected_dxbc_sha1')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'nested-fields')) 1 'evidence fields must be exactly'
    }

    Invoke-Test 'invalid evidence' {
        $manifest = New-ValidManifest
        $manifest.evidence.run_manifest_sha256 = ('A' * 64)
        $manifest.evidence.harness_version = 5
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'evidence-invalid')) 1 'lowercase 64-hex'
    }

    Invoke-Test 'fixed evidence hashes' {
        $manifest = New-ValidManifest
        $manifest.evidence.archive_sha256 = ('a' * 64)
        $manifest.evidence.archive_member_sha256 = ('b' * 64)
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'evidence-fixed')) 1 'required archive'
    }

    Invoke-Test 'malformed compiler' {
        $manifest = New-ValidManifest
        $manifest.compiler.name = 'dxc'
        $manifest.compiler.entry_point = 'other'
        $manifest.compiler.flags = @('/O3', '/nologo')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'compiler')) 1 'compiler.name must be exactly'
    }

    Invoke-Test 'wrong compiler field order' {
        $manifest = New-ValidManifest
        $valid = $manifest.compiler
        $manifest.compiler = [pscustomobject][ordered]@{
            name = $valid.name
            entry_point = $valid.entry_point
            include_root = $valid.include_root
            flags = $valid.flags
        }
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'compiler-order')) 1 'compiler fields must be exactly'
    }

    Invoke-Test 'missing compiler include root' {
        $manifest = New-ValidManifest
        $manifest.compiler.PSObject.Properties.Remove('include_root')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'include-root-missing')) 1 'compiler fields must be exactly'
    }

    Invoke-Test 'unknown compiler field' {
        $manifest = New-ValidManifest
        $manifest.compiler | Add-Member -NotePropertyName include_mode -NotePropertyValue 'source-directory'
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'compiler-unknown')) 1 'compiler fields must be exactly'
    }

    Invoke-Test 'invalid compiler include root' {
        $manifest = New-ValidManifest
        $manifest.compiler.include_root = 'repository'
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'include-root-invalid')) 1 'compiler.include_root must be exactly source-directory'
    }

    Invoke-Test 'bound compiler flag' {
        $manifest = New-ValidManifest
        $manifest.compiler.flags = @('/nologo', '/O3', '/Fo', 'out.dxbc')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'bound-flag')) 1 'must not bind'
    }

    Invoke-Test 'source-binding compiler flag' {
        $manifest = New-ValidManifest
        $manifest.compiler.flags = @('/nologo', '--source=shader.hlsl')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'path-flag')) 1 'must not bind'
    }

    Invoke-Test 'include-binding compiler flag' {
        $manifest = New-ValidManifest
        $manifest.compiler.flags = @('/nologo', '/O3', '/I', 'shaders/lighting')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'include-flag')) 1 'must not bind'
    }

    Invoke-Test 'malformed source and hash' {
        $manifest = New-ValidManifest
        $manifest.entries[0].source = '..\escape.hlsl'
        $manifest.entries[0].source_sha256 = 'ABC'
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'source-malformed')) 1 'normalized safe path'
    }

    Invoke-Test 'source hash mismatch before compile' {
        Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
        $manifest = New-ValidManifest
        $manifest.entries[0].source_sha256 = ('0' * 64)
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'source-mismatch')) 1 'does not match checked-in source'
        if (Test-Path -LiteralPath $marker) { throw 'fxc ran before source validation completed' }
    }

    Invoke-Test 'duplicate entry key and target' {
        $manifest = New-ValidManifest
        $manifest.entries += $manifest.entries[0]
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'duplicate-entry')) 1 'duplicates target|duplicates canonical key'
    }

    Invoke-Test 'unsorted entries' {
        $manifest = New-ValidManifest
        $first = $manifest.entries[0]
        $manifest.entries[0] = $manifest.entries[1]
        $manifest.entries[1] = $first
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'unsorted-entry')) 1 'entries must be sorted'
    }

    Invoke-Test 'unsorted defines' {
        $manifest = New-ValidManifest
        $manifest.entries[3].defines = @('LIGHT_TYPE=1', 'AMBIENT_IBL_IN_LIGHT=1')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'defines-unsorted')) 1 'defines must be sorted'
    }

    Invoke-Test 'duplicate defines' {
        $manifest = New-ValidManifest
        $manifest.entries[4].defines = @('LIGHT_TYPE=1', 'LIGHT_TYPE=1')
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'defines-duplicate')) 1 'duplicate define'
    }

    Invoke-Test 'missing target' {
        $manifest = New-ValidManifest
        $manifest.entries = @($manifest.entries | Where-Object { $_.target -cne 'deferred_prepass' })
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'target-missing')) 1 'missing required target deferred_prepass'
    }

    Invoke-Test 'extra target' {
        $manifest = New-ValidManifest
        $manifest.entries += New-Entry 'extra_target' 'shaders/lighting/extra.hlsl' @()
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'target-extra')) 1 'unexpected target extra_target'
    }

    Invoke-Test 'wrong canonical key' {
        $manifest = New-ValidManifest
        $manifest.entries[0] = New-Entry 'ambient_ibl_pass' 'shaders/lighting/alternate.hlsl' @()
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'canonical-key')) 1 'wrong canonical key|unexpected canonical key'
    }

    Invoke-Test 'missing compiler' {
        $missing = Join-Path $fixtureRoot 'missing-fxc.exe'
        Assert-Result (Invoke-Verifier (Write-Manifest (New-ValidManifest) 'missing-fxc') $missing) 2 'fxc not found'
    }

    Invoke-Test 'compiler failure aggregation' {
        Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
        $result = Invoke-Verifier (Write-Manifest (New-ValidManifest) 'compiler-failure') -CompilerFails
        Assert-Result $result 1 'FAIL: 8 / 8 shader round-trips failed'
        if (@(Get-Content -LiteralPath $marker).Count -ne 8) { throw 'compiler failures were not aggregated' }
    }

    Invoke-Test 'DXBC SHA mismatch' {
        $manifest = New-ValidManifest
        foreach ($entry in $manifest.entries) { $entry.expected_dxbc_sha1 = ('0' * 40) }
        Assert-Result (Invoke-Verifier (Write-Manifest $manifest 'dxbc-mismatch')) 1 'DXBC SHA1 mismatch'
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
