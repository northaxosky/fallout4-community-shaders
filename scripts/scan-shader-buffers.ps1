#Requires -Version 5.1
<#
.SYNOPSIS
    Refresh the generated register map in docs\shader-buffers.md and check for
    register collisions within a single compiled program.

.DESCRIPTION
    Copies the feature shaders and the reconstructed package shader tree into a
    throwaway scan root, runs hlslkit-buffer-scan there, and writes its report to
    docs\shader-buffers.md (a generated, gitignored doc).

    The scanner's own conflict list compares every shader against every other, but
    independent programs have independent register spaces, so most of what it
    reports is not a collision. The exit code therefore comes from a separate
    per-program check: this script resolves each root shader's transitive include
    set and fails only when one register is claimed twice inside the same program.
    That is the case that actually breaks a binding.

    Install the scanner with:
        pip install git+https://github.com/alandtse/hlslkit.git

.PARAMETER Check
    Do not write the doc; verify it already matches what a fresh scan would produce.
    Exits 3 if the doc is stale.

.EXAMPLE
    pwsh scripts\scan-shader-buffers.ps1

.EXAMPLE
    pwsh scripts\scan-shader-buffers.ps1 -Check
#>
[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent $scriptDir
$doc       = Join-Path $repoRoot 'docs\shader-buffers.md'

# Controlled exit with a preserved code. Write-Error would throw under
# $ErrorActionPreference='Stop' and collapse the code to 1, so go straight to stderr.
function Exit-WithError {
    param([string]$Message, [int]$Code)
    [Console]::Error.WriteLine("ERROR: $Message")
    exit $Code
}

function Get-IncludeTargets {
    param([string]$Path, [string[]]$AllFiles)
    $found = @()
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        if ($line -match '^\s*#\s*include\s+"([^"]+)"') {
            $leaf = Split-Path $Matches[1] -Leaf
            $found += $AllFiles | Where-Object { (Split-Path $_ -Leaf) -eq $leaf }
        }
    }
    return $found
}

# A root .hlsl plus everything it pulls in is one compiled program, and one
# register space. Collisions only matter inside that set.
function Get-ProgramFiles {
    param([string]$Root, [string[]]$AllFiles)
    $seen = [System.Collections.Generic.HashSet[string]]::new()
    $queue = [System.Collections.Generic.Queue[string]]::new()
    [void]$seen.Add($Root)
    $queue.Enqueue($Root)
    while ($queue.Count -gt 0) {
        foreach ($inc in (Get-IncludeTargets -Path $queue.Dequeue() -AllFiles $AllFiles)) {
            if ($seen.Add($inc)) { $queue.Enqueue($inc) }
        }
    }
    return $seen
}

function Get-RegisterClaims {
    param([string]$Path)
    $claims = @()
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($m in [regex]::Matches($text, '(?m)^[^/\r\n]*?\b(\w+)\s*:\s*register\s*\(\s*([bstu]\d+)\s*\)')) {
        $claims += [pscustomobject]@{ Name = $m.Groups[1].Value; Register = $m.Groups[2].Value }
    }
    return $claims
}

function Find-ProgramRegisterConflicts {
    param([string[]]$Roots, [string[]]$AllFiles)
    $conflicts = @()
    foreach ($root in $Roots) {
        $claims = @{}
        foreach ($file in (Get-ProgramFiles -Root $root -AllFiles $AllFiles)) {
            $leaf = Split-Path $file -Leaf
            foreach ($claim in (Get-RegisterClaims -Path $file)) {
                if (-not $claims.ContainsKey($claim.Register)) { $claims[$claim.Register] = @() }
                $claims[$claim.Register] += [pscustomobject]@{ File = $leaf; Name = $claim.Name }
            }
        }
        foreach ($key in $claims.Keys) {
            # Same-file repeats are permutation branches selected by a family macro,
            # so they never coexist. A slot claimed from two files is the real hazard.
            $files = $claims[$key] | Select-Object -ExpandProperty File -Unique
            if ($files.Count -gt 1) {
                $detail = ($claims[$key] | ForEach-Object { "$($_.Name) ($($_.File))" } | Sort-Object -Unique) -join ', '
                $conflicts += "$(Split-Path $root -Leaf) [$key]: $detail"
            }
        }
    }
    return $conflicts
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

    # The scanner compares every shader against every other, so its own conflict
    # list is dominated by independent programs that merely share a slot number.
    # Scope the verdict to collisions inside one program instead.
    $allFiles = Get-ChildItem -Path $scanDirs -Recurse -Include *.hlsl, *.hlsli -File |
        Select-Object -ExpandProperty FullName
    $roots = $allFiles | Where-Object { $_ -like '*.hlsl' }
    $conflicts = Find-ProgramRegisterConflicts -Roots $roots -AllFiles $allFiles

    if ($Check) {
        $current = if (Test-Path $doc) { Get-Content -Path $doc -Raw } else { $null }
        if (($null -ne $current) -and ($current -eq $report)) {
            Write-Host 'shader-buffers.md is current.'
        } else {
            Exit-WithError 'docs/shader-buffers.md is stale; run scripts/scan-shader-buffers.ps1 to refresh.' 3
        }
    } else {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $doc) | Out-Null
        Set-Content -Path $doc -Value $report -Encoding utf8 -NoNewline
        Write-Host "Wrote $doc"
    }

    if ($conflicts.Count -gt 0) {
        foreach ($c in $conflicts) { [Console]::Error.WriteLine("  $c") }
        Exit-WithError "$($conflicts.Count) register collision(s) within a single program." 1
    }
    Write-Host "No within-program register collisions across $($roots.Count) programs."
}
finally {
    Remove-Item -Recurse -Force -Path $scanRoot -ErrorAction SilentlyContinue
}
