<#
.SYNOPSIS
	Verifies and stages the SDK runtime DLLs that are not vendored in this repository.

.DESCRIPTION
	Reads scripts/sdk-manifest.psd1, downloads each pinned archive, verifies its SHA-256 against
	the manifest, and stages the required files into the mod package tree. Re-running is cheap:
	an archive whose digest already matches the cached copy is not downloaded again.

.PARAMETER CacheDirectory
	Where downloaded archives are kept. Defaults to <repo>/.sdk-cache.

.PARAMETER Force
	Re-download archives even when a verified cached copy exists.

.PARAMETER StreamlineCandidateDirectory
	Stage a signed candidate from the pinned Streamline fork instead of downloading its release.
	Requires the production-key verifier built by the fork's release workflow.

.PARAMETER PackageName
	Stage only the named manifest packages. Defaults to all packages.
#>
[CmdletBinding()]
param(
	[string]$CacheDirectory,
	[switch]$Force,
	[string]$StreamlineCandidateDirectory,
	[string[]]$PackageName
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $PSScriptRoot 'sdk-manifest.psd1'
if (-not (Test-Path -LiteralPath $manifestPath)) {
	throw "SDK manifest not found at $manifestPath"
}

if (-not $CacheDirectory) {
	$CacheDirectory = Join-Path $repoRoot '.sdk-cache'
}
New-Item -ItemType Directory -Force -Path $CacheDirectory | Out-Null

$manifest = Import-PowerShellDataFile -LiteralPath $manifestPath
$packages = @($manifest.Packages)
if ($PackageName) {
	foreach ($name in $PackageName) {
		if ($name -notin $packages.Name) {
			throw "Unknown SDK package '$name'. Available packages: $($packages.Name -join ', ')."
		}
	}
	$packages = @($packages | Where-Object { $_.Name -in $PackageName })
}

function Test-Digest {
	param([string]$Path, [string]$Expected)
	if (-not (Test-Path -LiteralPath $Path)) { return $false }
	$actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
	return $actual -ieq $Expected
}

function Get-Archive {
	param([hashtable]$Package)

	if (-not $Package.Url -or -not $Package.Sha256) {
		throw "[$($Package.Name)] $($Package.Version) has no published archive pin. Supply -StreamlineCandidateDirectory with its signed local candidate."
	}
	$archivePath = Join-Path $CacheDirectory ("{0}-{1}.zip" -f $Package.Name, $Package.Version)

	if (-not $Force -and (Test-Digest -Path $archivePath -Expected $Package.Sha256)) {
		Write-Host "[$($Package.Name)] cached archive digest matches; skipping download."
		return $archivePath
	}

	Write-Host "[$($Package.Name)] downloading $($Package.Url)"
	Invoke-WebRequest -Uri $Package.Url -OutFile $archivePath -UseBasicParsing

	if (-not (Test-Digest -Path $archivePath -Expected $Package.Sha256)) {
		$actual = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
		throw "[$($Package.Name)] SHA-256 mismatch. expected=$($Package.Sha256) actual=$actual"
	}

	Write-Host "[$($Package.Name)] digest verified."
	return $archivePath
}

function Get-StreamlineCandidate {
	param([hashtable]$Package)

	$fork = Join-Path $repoRoot 'extern\Streamline'
	$config = Import-PowerShellDataFile -LiteralPath (Join-Path $fork 'config\project-release.psd1')
	if ($Package.Version -cne $config.ReleaseTag) {
		throw "Streamline package version does not match the pinned fork release configuration."
	}
	$candidate = (Resolve-Path -LiteralPath $StreamlineCandidateDirectory).Path
	$packageRoot = Join-Path $candidate 'package'
	$runtime = Join-Path $packageRoot 'bin\x64'
	foreach ($name in @('sl.project-manifest.bin', 'sl.project-manifest.sig')) {
		if (-not (Test-Path -LiteralPath (Join-Path $runtime $name) -PathType Leaf)) {
			throw "Signed Streamline candidate is incomplete: $name is missing."
		}
	}
	$verifier = Join-Path $fork '_artifacts\project-release\verifier\project-release-verifier.exe'
	if (-not (Test-Path -LiteralPath $verifier -PathType Leaf)) {
		throw "Build the pinned Streamline release workflow's production-key verifier before staging a local candidate."
	}
	$sourceCommit = & git -C $fork rev-parse HEAD
	if ($LASTEXITCODE -ne 0) {
		throw "Cannot determine the pinned Streamline source commit."
	}
	& (Join-Path $fork 'tools\project-release.ps1') -Mode Validate `
		-CandidateDirectory $candidate -SourceCommit $sourceCommit.Trim() | Out-Host
	& $verifier $runtime | Out-Host
	if ($LASTEXITCODE -ne 0) {
		throw "Streamline candidate failed production signature and payload verification."
	}
	return $packageRoot
}

function Copy-StagedFile {
	param([string]$ExtractRoot, [string]$FileName, [string]$Destination, [bool]$Required)

	# Entries are archive-relative paths: SDK archives ship the same file name in several
	# places, including watermarked development builds under bin/x64/development.
	$leaf = Split-Path -Path $FileName -Leaf
	$relative = ($FileName -replace '/', '\')
	$rootPrefix = (Resolve-Path -LiteralPath $ExtractRoot).Path.TrimEnd('\') + '\'

	$found = @(Get-ChildItem -LiteralPath $ExtractRoot -Recurse -File -Filter $leaf |
		Where-Object {
			$_.FullName.Substring($rootPrefix.Length).Equals($relative, [System.StringComparison]::OrdinalIgnoreCase)
		})

	if ($found.Count -eq 0) {
		if ($Required) {
			throw "Required file '$FileName' was not found in the archive."
		}
		Write-Warning "Optional file '$FileName' was not found in the archive."
		return
	}

	Copy-Item -LiteralPath $found[0].FullName -Destination (Join-Path $Destination $leaf) -Force
	Write-Host "  staged $leaf"
}

foreach ($package in $packages) {
	$localCandidate = $package.Name -eq 'Streamline' -and $StreamlineCandidateDirectory
	if ($localCandidate) {
		$extractRoot = Get-StreamlineCandidate -Package $package
	} else {
		$archivePath = Get-Archive -Package $package
		$extractRoot = Join-Path $CacheDirectory ("{0}-{1}-extracted" -f $package.Name, $package.Version)
		if (Test-Path -LiteralPath $extractRoot) {
			Remove-Item -LiteralPath $extractRoot -Recurse -Force
		}
		Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot -Force
	}

	$destination = Join-Path $repoRoot $package.Destination
	New-Item -ItemType Directory -Force -Path $destination | Out-Null

	foreach ($file in $package.Files) {
		Copy-StagedFile -ExtractRoot $extractRoot -FileName $file -Destination $destination -Required $true
	}
	foreach ($license in $package.Licenses) {
		Copy-StagedFile -ExtractRoot $extractRoot -FileName $license -Destination $destination -Required $true
	}
	if ($package.ContainsKey('Headers')) {
		$headerDestination = Join-Path $repoRoot $package.HeaderDestination
		New-Item -ItemType Directory -Force -Path $headerDestination | Out-Null
		foreach ($header in $package.Headers) {
			Copy-StagedFile -ExtractRoot $extractRoot -FileName $header -Destination $headerDestination -Required $true
		}
	}

	if ($localCandidate) {
		$verifier = Join-Path $repoRoot 'extern\Streamline\_artifacts\project-release\verifier\project-release-verifier.exe'
		& $verifier $destination
		if ($LASTEXITCODE -ne 0) {
			throw "Staged Streamline runtime failed production verification."
		}
	} else {
		Remove-Item -LiteralPath $extractRoot -Recurse -Force
	}
	Write-Host "[$($package.Name)] staged into $($package.Destination)"
}

Write-Host 'SDK staging complete.'
