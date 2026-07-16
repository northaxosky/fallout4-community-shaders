#requires -Version 5.0
<#
.SYNOPSIS
    Verify that reconstructed deferred-pipeline HLSL files round-trip to
    their canonical fxc sha1 baselines.

.DESCRIPTION
    Regression protection on the spec. For each tracked HLSL file in
    shaders/lighting/, compile with fxc /T ps_5_0 /O3 and compare the
    output dxbc sha1 against the stored baseline in
    scripts/shaders/shader-roundtrip-baselines.json.

    This does NOT validate that the HLSL matches the original game corpus
    asm; the corpus-match tolerance is documented in
    .agents/shader-reconstruction-coverage.md. This validates that future
    edits don't silently change fxc output for the tracked spec.

    The DIRECTIONAL bsdf permutation has a hard sha1 invariant
    (085625AF...) locked by commit 5845b9e. The script flags it specially.

.PARAMETER UpdateBaselines
    Re-capture the baselines after a deliberate refactor. Writes the
    compiled sha1s back into scripts/shaders/shader-roundtrip-baselines.json.

.PARAMETER FxcPath
    Override the fxc.exe path. Default is the Windows 10 SDK
    10.0.26100.0 location.

.EXAMPLE
    .\scripts\shaders\verify-shader-roundtrip.ps1
    # Validate all tracked shaders against baselines. Exit 0 on PASS.

.EXAMPLE
    .\scripts\shaders\verify-shader-roundtrip.ps1 -UpdateBaselines
    # Re-capture baselines after an intentional refactor.

.NOTES
    TODO: The open `followup-ci-workflows` todo should invoke this from CI
    once GitHub Actions workflows land in this repo. Until then it's
    manually-run regression protection.
#>

[CmdletBinding()]
param(
    [switch]$UpdateBaselines,
    [string]$FxcPath = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent (Split-Path -Parent $scriptDir)
$baselinePath = Join-Path $scriptDir "shader-roundtrip-baselines.json"

if (-not (Test-Path $FxcPath)) {
    Write-Host "ERROR: fxc not found at $FxcPath" -ForegroundColor Red
    Write-Host "Pass -FxcPath <path> or install Windows 10 SDK 10.0.26100.0."
    exit 2
}
if (-not (Test-Path $baselinePath)) {
    Write-Host "ERROR: baselines file not found at $baselinePath" -ForegroundColor Red
    exit 2
}

$config = Get-Content $baselinePath -Raw | ConvertFrom-Json
$tmp = Join-Path $repoRoot ".shader-cache\roundtrip"
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
New-Item -ItemType Directory -Path $tmp -Force | Out-Null

# Wrap the body so the scratch dir is removed on every exit path; a `finally`
# still runs when the script terminates via `exit`.
try {

$results = @()
foreach ($shader in $config.shaders) {
    $src = Join-Path $repoRoot $shader.src
    $out = Join-Path $tmp "$($shader.name).dxbc"
    $defineArgs = @()
    foreach ($d in $shader.defines) { $defineArgs += "/D$d" }
    $fxcArgs = @("/nologo", "/T", "ps_5_0", "/O3", "/E", "main") + $defineArgs + @("/Fo", $out, $src)

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $fxcOutput = & $FxcPath @fxcArgs 2>&1
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($LASTEXITCODE -ne 0) {
        $results += [pscustomobject]@{
            Name     = $shader.name
            Defines  = ($shader.defines -join ",")
            Expected = $shader.sha1
            Actual   = "<compile failed>"
            Status   = "FAIL"
            Details  = ($fxcOutput | Out-String)
        }
        continue
    }
    $sha = (Get-FileHash -Algorithm SHA1 $out).Hash
    $status = if ($sha -eq $shader.sha1) { "PASS" } else { "FAIL" }
    $results += [pscustomobject]@{
        Name     = $shader.name
        Defines  = ($shader.defines -join ",")
        Expected = $shader.sha1
        Actual   = $sha
        Status   = $status
        Details  = $null
    }
}

if ($UpdateBaselines) {
    foreach ($shader in $config.shaders) {
        $r = $results | Where-Object { $_.Name -eq $shader.name } | Select-Object -First 1
        if ($r -and $r.Actual -match '^[0-9A-F]{40}$') {
            $shader.sha1 = $r.Actual
        }
    }
    $config | ConvertTo-Json -Depth 8 | Set-Content -Path $baselinePath -Encoding ASCII
    Write-Host "Baselines updated: $baselinePath" -ForegroundColor Yellow
    $results | Format-Table Name, Defines, Actual -AutoSize
    exit 0
}

$fails = $results | Where-Object { $_.Status -ne "PASS" }
if ($fails.Count -eq 0) {
    Write-Host ("PASS: {0} / {0} shaders round-trip cleanly." -f $results.Count) -ForegroundColor Green
    exit 0
}

Write-Host ""
Write-Host ("FAIL: {0} / {1} shaders drifted from baseline." -f $fails.Count, $results.Count) -ForegroundColor Red
Write-Host ""
$results | Format-Table Name, Defines, Status, Expected, Actual -AutoSize
foreach ($f in $fails) {
    Write-Host "--- $($f.Name) ---" -ForegroundColor Red
    Write-Host "  expected: $($f.Expected)"
    Write-Host "  actual:   $($f.Actual)"
    if ($f.Details) { Write-Host $f.Details }
    if ($f.Name -like "*directional*") {
        Write-Host "  NOTE: DIRECTIONAL bsdf has a hard sha1 invariant from commit 5845b9e." -ForegroundColor Yellow
        Write-Host "        If this fails, revert the refactor rather than re-baseline." -ForegroundColor Yellow
    }
}
Write-Host ""
Write-Host "If this drift is intentional (deliberate refactor), re-baseline with:"
Write-Host "  .\scripts\shaders\verify-shader-roundtrip.ps1 -UpdateBaselines"
exit 1

}
finally {
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
}
