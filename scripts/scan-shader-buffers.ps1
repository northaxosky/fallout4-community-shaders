#Requires -Version 5.1
<#
.SYNOPSIS
    Run hlslkit-buffer-scan across the canonical shader roots and refresh
    docs\shader-buffers.md. Exits nonzero if any cross-feature register conflict
    is detected.

.DESCRIPTION
    Copies the feature shaders and the reconstructed package shader tree into a
    throwaway scan root, runs the scanner there, and
    writes its report to docs\shader-buffers.md (a generated, gitignored doc).

    Install the scanner with:
        pip install git+https://github.com/alandtse/hlslkit.git

.PARAMETER Check
    Do not write the doc; verify it already matches what a fresh scan would produce.
    Exits 3 if the doc is stale.

.EXAMPLE
    pwsh scripts\shaders\scan-shader-buffers.ps1

.EXAMPLE
    pwsh scripts\shaders\scan-shader-buffers.ps1 -Check
#>
[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent (Split-Path -Parent $scriptDir)
$doc       = Join-Path $repoRoot 'docs\shader-buffers.md'

# Controlled exit with a preserved code. Write-Error would throw under
# $ErrorActionPreference='Stop' and collapse the code to 1, so go straight to stderr.
function Exit-WithError {
    param([string]$Message, [int]$Code)
    [Console]::Error.WriteLine("ERROR: $Message")
    exit $Code
}

if (-not (Get-Command hlslkit-buffer-scan -ErrorAction SilentlyContinue)) {
    Exit-WithError 'hlslkit-buffer-scan not on PATH. Install with: pip install git+https://github.com/alandtse/hlslkit.git' 2
}

$scanDirs = @(
    (Join-Path $repoRoot 'features'),
    (Join-Path $repoRoot 'package\Shaders')
)
foreach ($d in $scanDirs) {
    if (-not (Test-Path $d)) { Exit-WithError "required shader scan directory not found: $d" 2 }
}

$scanRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('shaderscan-' + [System.Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Force -Path (Join-Path $scanRoot 'features') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $scanRoot 'package') | Out-Null

    Copy-Item -Recurse -Force -Path (Join-Path $repoRoot 'features\*')  -Destination (Join-Path $scanRoot 'features')
    Copy-Item -Recurse -Force -Path (Join-Path $repoRoot 'package\Shaders') -Destination (Join-Path $scanRoot 'package')

    Push-Location $scanRoot
    try {
        $report = (& hlslkit-buffer-scan --show-conflicts | Out-String)
        $scanExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($scanExit -ne 0) {
        Exit-WithError "hlslkit-buffer-scan failed (exit code $scanExit)" 2
    }

    # The scanner prints a fixed "No register conflicts detected" line when clean; any
    # other content under its Register Conflicts header means a real collision.
    $hasConflict = -not ($report -match '(?m)^No register conflicts detected')

    if ($Check) {
        $current = if (Test-Path $doc) { Get-Content -Path $doc -Raw } else { $null }
        if (($null -ne $current) -and ($current -eq $report)) {
            Write-Host 'shader-buffers.md is current.'
        } else {
            Exit-WithError 'docs/shader-buffers.md is stale; run scripts/shaders/scan-shader-buffers.ps1 to refresh.' 3
        }
    } else {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $doc) | Out-Null
        Set-Content -Path $doc -Value $report -Encoding utf8 -NoNewline
        Write-Host "Wrote $doc"
    }

    if ($hasConflict) {
        Exit-WithError "REGISTER CONFLICT detected. See Register Conflicts section of $doc." 1
    }
    Write-Host 'No register conflicts.'
}
finally {
    Remove-Item -Recurse -Force -Path $scanRoot -ErrorAction SilentlyContinue
}
