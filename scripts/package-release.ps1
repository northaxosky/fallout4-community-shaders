[CmdletBinding()]
param(
	[Parameter(Mandatory)]
	[ValidateNotNullOrEmpty()]
	[string]$Identifier,

	[string]$Version,

	[string]$OutputDirectory,

	[switch]$KeepStaging
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Version) {
	$Version = (Get-Content -LiteralPath (Join-Path $repoRoot 'version.txt') -Raw).Trim()
}
if (-not $OutputDirectory) {
	$OutputDirectory = Join-Path $repoRoot 'build/dist'
}

function Assert-SourcePath {
	param([string]$Path)
	if (-not (Test-Path -LiteralPath $Path)) {
		throw "Required package source not found: $Path"
	}
}

function Assert-PackageFile {
	param([string]$RelativePath)
	$path = Join-Path $stagingRoot $RelativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Required package file not found: $RelativePath"
	}
}

$safeVersion = $Version -replace '[^A-Za-z0-9._-]', '-'
$safeIdentifier = $Identifier -replace '[^A-Za-z0-9._-]', '-'
if (-not $safeVersion -or -not $safeIdentifier) {
	throw 'Version and identifier must contain at least one file-name-safe character.'
}

$packageName = "FO4CommunityShaders-$safeVersion-$safeIdentifier"
$stagingRoot = Join-Path $OutputDirectory $packageName
$archivePath = Join-Path $OutputDirectory "$packageName.zip"
$f4seSource = Join-Path $repoRoot 'package/F4SE'
$shaderSource = Join-Path $repoRoot 'build/ShaderStage/Shaders'
$pluginSource = Join-Path $repoRoot 'build/Release/FO4CommunityShaders.dll'

Assert-SourcePath $f4seSource
Assert-SourcePath $shaderSource
Assert-SourcePath $pluginSource

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stagingRoot | Out-Null

Copy-Item -LiteralPath $f4seSource -Destination $stagingRoot -Recurse
Copy-Item -LiteralPath $shaderSource -Destination $stagingRoot -Recurse

$pluginDestination = Join-Path $stagingRoot 'F4SE/Plugins'
New-Item -ItemType Directory -Force -Path $pluginDestination | Out-Null
Copy-Item -LiteralPath $pluginSource -Destination $pluginDestination
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $stagingRoot
Copy-Item -LiteralPath (Join-Path $repoRoot 'EXCEPTIONS.md') -Destination $stagingRoot

Get-ChildItem -LiteralPath $stagingRoot -Recurse -File -Filter '.gitkeep' |
	Remove-Item -Force
Remove-Item -LiteralPath (Join-Path $stagingRoot 'Shaders/SharedDataProbe.hlsl') -Force -ErrorAction SilentlyContinue

$requiredFiles = @(
	'F4SE/Plugins/FO4CommunityShaders.dll',
	'LICENSE',
	'EXCEPTIONS.md',
	'Shaders/Upscaling/Streamline/nvngx_dlss.dll',
	'Shaders/Upscaling/Streamline/sl.interposer.dll',
	'Shaders/Upscaling/Streamline/sl.common.dll',
	'Shaders/Upscaling/Streamline/sl.dlss.dll',
	'Shaders/Upscaling/FidelityFX/amd_fidelityfx_framegeneration_dx12.dll',
	'Shaders/Upscaling/FidelityFX/amd_fidelityfx_loader_dx12.dll'
)
foreach ($relativePath in $requiredFiles) {
	Assert-PackageFile $relativePath
}

$requiredLicenses = @(
	'Shaders/Upscaling/Streamline/license.txt',
	'Shaders/Upscaling/Streamline/nvngx_dlss.license.txt',
	'Shaders/Upscaling/FidelityFX/license.md'
)
foreach ($relativePath in $requiredLicenses) {
	Assert-PackageFile $relativePath
}

$requiredShaderDirectories = @(
	'Shaders/Common',
	'Shaders/ScreenSpaceGI',
	'Shaders/ScreenSpaceShadows',
	'Shaders/TerrainShadows',
	'Shaders/Upscaling',
	'Shaders/InverseSquareLighting',
	'Shaders/DynamicCubemaps',
	'Shaders/WetnessEffects',
	'Shaders/WaterEffects'
)
foreach ($relativePath in $requiredShaderDirectories) {
	$path = Join-Path $stagingRoot $relativePath
	if (-not (Get-ChildItem -LiteralPath $path -Recurse -File -ErrorAction SilentlyContinue)) {
		throw "Required shader directory is empty or missing: $relativePath"
	}
}

$forbiddenFiles = @(Get-ChildItem -LiteralPath $stagingRoot -Recurse -File |
	Where-Object {
		$_.Extension -ieq '.pdb' -or
		$_.Name -like '*.User.toml' -or
		$_.Name -eq '.gitkeep' -or
		$_.Name -eq 'SharedDataProbe.hlsl'
	})
if ($forbiddenFiles.Count -gt 0) {
	throw "Package contains forbidden files: $($forbiddenFiles.FullName -join ', ')"
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$archive = [System.IO.Compression.ZipFile]::Open(
	$archivePath,
	[System.IO.Compression.ZipArchiveMode]::Create)
try {
	$files = @(Get-ChildItem -LiteralPath $stagingRoot -Recurse -File |
		ForEach-Object {
			[pscustomobject]@{
				ArchivePath = [System.IO.Path]::GetRelativePath($stagingRoot, $_.FullName).Replace('\', '/')
				SourcePath = $_.FullName
			}
		} |
		Sort-Object -Property ArchivePath -CaseSensitive)

	$timestamp = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
	foreach ($file in $files) {
		$entry = $archive.CreateEntry(
			$file.ArchivePath,
			[System.IO.Compression.CompressionLevel]::Optimal)
		$entry.LastWriteTime = $timestamp
		$inputStream = [System.IO.File]::OpenRead($file.SourcePath)
		$outputStream = $entry.Open()
		try {
			$inputStream.CopyTo($outputStream)
		} finally {
			$outputStream.Dispose()
			$inputStream.Dispose()
		}
	}
} finally {
	$archive.Dispose()
}

if (-not $KeepStaging) {
	Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}

Write-Host "Created $archivePath"
$archivePath
