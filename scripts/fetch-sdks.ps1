<#
.SYNOPSIS
	Verifies and stages the SDK runtime DLLs that are not vendored in this repository.

.DESCRIPTION
	Reads scripts/sdk-manifest.psd1, downloads each archive, verifies its SHA-256 against the
	manifest pin or, for Repository packages, the latest release's SHA256SUMS.txt, and stages
	the required files into the mod package tree. Re-running is cheap: an archive whose digest
	already matches the cached copy is not downloaded again.

.PARAMETER CacheDirectory
	Where downloaded archives are kept. Defaults to <repo>/.sdk-cache.

.PARAMETER Force
	Re-download archives even when a verified cached copy exists.

.PARAMETER PackageName
	Stage only the named manifest packages. Defaults to all packages.
#>
[CmdletBinding()]
param(
	[string]$CacheDirectory,
	[switch]$Force,
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

	if ($Package.ContainsKey('Repository')) {
		Resolve-LatestRelease -Package $Package
	}
	if (-not $Package.Url -or -not $Package.Sha256) {
		throw "[$($Package.Name)] has no archive URL and SHA-256 pin."
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

function Resolve-LatestRelease {
	param([hashtable]$Package)

	$headers = @{ Accept = 'application/vnd.github+json' }
	$token = if ($env:GH_TOKEN) { $env:GH_TOKEN } else { $env:GITHUB_TOKEN }
	if ($token) { $headers.Authorization = "Bearer $token" }
	$release = Invoke-RestMethod -Headers $headers `
		-Uri "https://api.github.com/repos/$($Package.Repository)/releases/latest"
	$base = "https://github.com/$($Package.Repository)/releases/download/$($release.tag_name)"
	$sums = (Invoke-WebRequest -UseBasicParsing -Uri "$base/SHA256SUMS.txt").Content
	if ($sums -is [byte[]]) { $sums = [Text.Encoding]::UTF8.GetString($sums) }
	$line = $sums -split "`r?`n" | Where-Object { $_ -match "^([0-9a-fA-F]{64})\s+$([regex]::Escape($Package.Asset))$" } |
		Select-Object -First 1
	if (-not $line) {
		throw "[$($Package.Name)] $($release.tag_name) SHA256SUMS.txt has no entry for $($Package.Asset)."
	}
	$Package.Version = $release.tag_name
	$Package.Url = "$base/$($Package.Asset)"
	$Package.Sha256 = ($line -split '\s+')[0]
	Write-Host "[$($Package.Name)] latest release $($Package.Version)"
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
	$archivePath = Get-Archive -Package $package
	$extractRoot = Join-Path $CacheDirectory ("{0}-{1}-extracted" -f $package.Name, $package.Version)
	if (Test-Path -LiteralPath $extractRoot) {
		Remove-Item -LiteralPath $extractRoot -Recurse -Force
	}
	Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot -Force

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

	Remove-Item -LiteralPath $extractRoot -Recurse -Force
	Write-Host "[$($package.Name)] staged into $($package.Destination)"
}

Write-Host 'SDK staging complete.'
