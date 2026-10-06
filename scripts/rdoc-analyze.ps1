#Requires -Version 5.1
<#
.SYNOPSIS
    Analyze a D3D11 or D3D12 RenderDoc capture without opening the RenderDoc UI.

.DESCRIPTION
    Resolves a Fallout 4 capture, writes a JSON job, and runs the bundled Python
    analyzer in qrenderdoc.exe. Results and image artifacts are written
    outside the repository.

.PARAMETER Capture
    Capture path or a name substring. Defaults to the newest capture.

.PARAMETER Command
    Analysis command. Defaults to overview. `script <file.py> [args...]` runs an ad-hoc probe
    that defines run(session, actions, args, out_dir) under the same replay lifecycle.

.PARAMETER CommandArgs
    Arguments passed to the selected analysis command.

.PARAMETER OutputDir
    Artifact directory. Defaults to a unique temporary directory. progress.log there records
    the last completed stage of a slow or timed-out run.

.PARAMETER RenderDocPath
    Path to qrenderdoc.exe or its installation directory.

.PARAMETER TimeoutSeconds
    Maximum analysis time before the replay is killed.

.PARAMETER List
    List captures in the default capture directory.

.EXAMPLE
    pwsh scripts\rdoc-analyze.ps1

.EXAMPLE
    pwsh scripts\rdoc-analyze.ps1 FO4_frame67748 overview

.EXAMPLE
    pwsh scripts\rdoc-analyze.ps1 FO4_frame67748 stats ScreenSpaceShadows/Mask.Texture

.EXAMPLE
    pwsh scripts\rdoc-analyze.ps1 FO4_frame67748 cbuffer 110 compute --slot 0 --space 0

.EXAMPLE
    pwsh scripts\rdoc-analyze.ps1 FO4_frame67748 script $env:TEMP\probe.py 55259
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Capture,
    [Parameter(Position = 1)]
    [string]$Command = 'overview',
    [Parameter(Position = 2, ValueFromRemainingArguments = $true)]
    [string[]]$CommandArgs = @(),
    [string]$OutputDir,
    [string]$RenderDocPath,
    [ValidateRange(1, 3600)]
    [int]$TimeoutSeconds = 300,
    [switch]$List
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$analyzer = Join-Path $scriptDir 'rdoc\analyze.py'
$captureDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\Fallout4\F4SE\FO4CommunityShaders\captures'

function Exit-WithJsonError {
    param([string]$Message, [int]$Code, [string]$Artifacts)
    $payload = [ordered]@{
        ok = $false
        command = $Command
        artifactDirectory = $Artifacts
        error = $Message
    }
    Write-Output ($payload | ConvertTo-Json -Depth 5)
    if ($Artifacts) { Write-Output "Artifacts: $Artifacts" }
    exit $Code
}

function Resolve-QRenderDoc {
    $candidates = @()
    if ($RenderDocPath) { $candidates += $RenderDocPath }
    if ($env:RENDERDOC_PATH) { $candidates += $env:RENDERDOC_PATH }
    $candidates += 'C:\Program Files\RenderDoc'
    foreach ($candidate in $candidates) {
        $path = if ([IO.Path]::GetExtension($candidate) -eq '.exe') {
            $candidate
        } else {
            Join-Path $candidate 'qrenderdoc.exe'
        }
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }
    return $null
}

function Get-Captures {
    if (-not (Test-Path -LiteralPath $captureDir -PathType Container)) { return @() }
    return @(Get-ChildItem -LiteralPath $captureDir -Filter '*.rdc' -File |
        Sort-Object LastWriteTime -Descending)
}

function Test-InsideRepository {
    param([string]$Path)
    $candidate = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $root = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/')
    return $candidate.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
        $candidate.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Stop-ProcessTree {
    param([Diagnostics.Process]$Process)
    try {
        $Process.Kill($true)
    } catch {
        & "$env:SystemRoot\System32\taskkill.exe" /PID $Process.Id /T /F 2>$null | Out-Null
    }
    # A replay stuck in GPU driver teardown may never finish exiting; never wait on it unbounded.
    return $Process.WaitForExit(10000)
}

function Get-LastProgress {
    $progressPath = Join-Path $OutputDir 'progress.log'
    if (Test-Path -LiteralPath $progressPath -PathType Leaf) {
        return (Get-Content -LiteralPath $progressPath -Tail 1)
    }
    return $null
}

if ($List) {
    $captures = Get-Captures
    if ($captures.Count -eq 0) {
        Exit-WithJsonError "No captures found in $captureDir" 2 $null
    }
    $captures | Select-Object Name,
        @{Name = 'SizeGiB'; Expression = { [math]::Round($_.Length / 1GB, 3) }},
        LastWriteTime, FullName | Format-Table -AutoSize
    exit 0
}

if (-not (Test-Path -LiteralPath $analyzer -PathType Leaf)) {
    Exit-WithJsonError "Analyzer not found: $analyzer" 2 $null
}
if (-not $OutputDir) {
    $OutputDir = Join-Path ([IO.Path]::GetTempPath()) (
        'fo4cs-rdoc-' + [Guid]::NewGuid().ToString('N'))
}
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
if (Test-InsideRepository $OutputDir) {
    Exit-WithJsonError 'Artifact directory must be outside the repository.' 2 $null
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$capturePath = $null
if ($Capture -and (Test-Path -LiteralPath $Capture -PathType Leaf)) {
    $capturePath = (Resolve-Path -LiteralPath $Capture).Path
} elseif ($Capture -and (
    [IO.Path]::IsPathRooted($Capture) -or
    $Capture.Contains('\') -or
    $Capture.Contains('/') -or
    [IO.Path]::GetExtension($Capture) -eq '.rdc')) {
    $capturePath = [IO.Path]::GetFullPath($Capture)
} else {
    $captures = Get-Captures
    if ($Capture) {
        $captures = @($captures | Where-Object { $_.Name -like "*$Capture*" })
    }
    if ($captures.Count -eq 0) {
        $detail = if ($Capture) { " matching '$Capture'" } else { '' }
        Exit-WithJsonError "No captures$detail found in $captureDir" 2 $OutputDir
    }
    $capturePath = $captures[0].FullName
}

$qrenderdoc = Resolve-QRenderDoc
if (-not $qrenderdoc) {
    Exit-WithJsonError 'qrenderdoc.exe not found. Set -RenderDocPath or RENDERDOC_PATH.' 2 $OutputDir
}

$jobPath = Join-Path $OutputDir 'job.json'
$resultPath = Join-Path $OutputDir 'result.json'
Remove-Item -LiteralPath $resultPath, (Join-Path $OutputDir 'progress.log') -Force -ErrorAction SilentlyContinue
$job = [ordered]@{
    capture = $capturePath
    command = $Command.ToLowerInvariant()
    args = @($CommandArgs)
    outDir = $OutputDir
    repoRoot = $repoRoot
}
$jobJson = $job | ConvertTo-Json -Depth 10
[IO.File]::WriteAllText(
    $jobPath,
    $jobJson,
    [Text.UTF8Encoding]::new($false))

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $qrenderdoc
$startInfo.Arguments = '--python "' + $analyzer + '"'
# qrenderdoc starts a daemonized adb server; ShellExecute inherits no handles, so it cannot pin
# the caller's output pipe open. Hidden suppresses the Qt window while --python runs.
$startInfo.UseShellExecute = $true
$startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$env:RDOC_JOB = $jobPath
$env:RDOC_SCRIPT_DIR = Split-Path -Parent $analyzer
$process = [Diagnostics.Process]::new()
$process.StartInfo = $startInfo

$replayLingered = $false
try {
    if (-not $process.Start()) {
        Exit-WithJsonError 'qrenderdoc.exe did not start.' 2 $OutputDir
    }
    # The result file, not process exit, marks completion: replay teardown can hang indefinitely.
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.WaitForExit(500)) {
        if (Test-Path -LiteralPath $resultPath -PathType Leaf) {
            if (-not $process.WaitForExit(10000)) {
                $replayLingered = -not (Stop-ProcessTree $process)
            }
            break
        }
        if ([DateTime]::UtcNow -ge $deadline) {
            $exited = Stop-ProcessTree $process
            $stage = Get-LastProgress
            $detail = if ($stage) { " Last progress: $stage." } else { '' }
            $reaped = if ($exited) { '' } else { " Replay process $($process.Id) did not exit after kill." }
            Exit-WithJsonError "Analysis timed out after $TimeoutSeconds seconds.$detail$reaped" 3 $OutputDir
        }
    }
} finally {
    $process.Dispose()
}

if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
    $stage = Get-LastProgress
    $detail = if ($stage) { " Last progress: $stage." } else { '' }
    Exit-WithJsonError "Analyzer did not produce result.json.$detail" 2 $OutputDir
}
$resultJson = Get-Content -LiteralPath $resultPath -Raw
Write-Output $resultJson
if ($replayLingered) { Write-Warning 'The replay process did not exit after writing its result.' }
Write-Output "Artifacts: $OutputDir"
$result = $resultJson | ConvertFrom-Json
if (-not $result.ok) { exit 1 }
