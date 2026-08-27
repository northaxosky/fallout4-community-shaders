<#
.SYNOPSIS
	Downloads and stages the proprietary runtime DLLs that are not vendored in this repository.

.DESCRIPTION
	Reads scripts/sdk-manifest.psd1, downloads each pinned archive, verifies its SHA-256 against
	the manifest, and stages the required files into the mod package tree. Re-running is cheap:
	an archive whose digest already matches the cached copy is not downloaded again.

.PARAMETER CacheDirectory
	Where downloaded archives are kept. Defaults to <repo>/.sdk-cache.

.PARAMETER Force
	Re-download archives even when a verified cached copy exists.
#>
[CmdletBinding()]
param(
	[string]$CacheDirectory,
	[switch]$Force
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

function Test-Digest {
	param([string]$Path, [string]$Expected)
	if (-not (Test-Path -LiteralPath $Path)) { return $false }
	$actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
	return $actual -ieq $Expected
}

function Get-Archive {
	param([hashtable]$Package)

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

function Copy-StagedFile {
	param([string]$ExtractRoot, [string]$FileName, [string]$Destination, [bool]$Required)

	$match = Get-ChildItem -LiteralPath $ExtractRoot -Recurse -File -Filter $FileName |
		Sort-Object FullName |
		Select-Object -First 1

	if (-not $match) {
		if ($Required) {
			throw "Required file '$FileName' was not found in the archive."
		}
		Write-Warning "Optional file '$FileName' was not found in the archive."
		return
	}

	Copy-Item -LiteralPath $match.FullName -Destination (Join-Path $Destination $FileName) -Force
	Write-Host "  staged $FileName"
}

foreach ($package in $manifest.Packages) {
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

	Remove-Item -LiteralPath $extractRoot -Recurse -Force
	Write-Host "[$($package.Name)] staged into $($package.Destination)"
}

Write-Host 'SDK staging complete.'
