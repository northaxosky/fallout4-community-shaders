[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $RepoRoot,

    [Parameter(Mandatory = $true)]
    [string] $ProjectConfig
)

$ErrorActionPreference = 'Stop'
$expectedFrom = 'package\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows\*.json'
$expectedTo = 'F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows'
$artifactRelative = 'package\F4SE\Plugins\FO4CommunityShaders\ScreenSpaceShadows\sss-dxbc-patch-plans.json'
$expectedLength = 53601
$expectedSha256 = 'eaa508727cd08ed20b9d3c5db823eebe28e23b78f948e179dab84ae9217d8483'

$root = [IO.Path]::GetFullPath($RepoRoot)
$configPath = [IO.Path]::GetFullPath($ProjectConfig)
$project = Import-PowerShellDataFile -LiteralPath $configPath
$entries = @($project.Deploy.Config | Where-Object {
    $_.From -eq $expectedFrom -and $_.To -eq $expectedTo
})
if ($entries.Count -ne 1) {
    throw "Devkit deploy contract must contain exactly one SSS artifact entry: From='$expectedFrom'; To='$expectedTo'."
}
$entry = $entries[0]
if ($entry.ContainsKey('Optional') -and [bool] $entry.Optional) {
    throw 'The SSS patch artifact deploy entry must not be optional.'
}

$artifactPath = Join-Path $root $artifactRelative
$artifact = Get-Item -LiteralPath $artifactPath
if ($artifact.Length -ne $expectedLength) {
    throw "Packaged SSS patch artifact length mismatch: $($artifact.Length)."
}
$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifactPath).Hash.ToLowerInvariant()
if ($sourceHash -ne $expectedSha256) {
    throw "Packaged SSS patch artifact SHA-256 mismatch: $sourceHash."
}

$matchedFiles = @(Get-ChildItem -Path (Join-Path $root $entry.From) -File)
$artifactFullPath = [IO.Path]::GetFullPath($artifactPath)
if (-not ($matchedFiles.FullName | Where-Object {
    [IO.Path]::GetFullPath($_) -eq $artifactFullPath
})) {
    throw 'The devkit source glob does not include the packaged SSS patch artifact.'
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("fo4cs-sss-deploy-contract-" + [guid]::NewGuid().ToString('N'))
try {
    $destination = Join-Path $temporaryRoot $entry.To
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($file in $matchedFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
    $deployedArtifact = Join-Path $destination 'sss-dxbc-patch-plans.json'
    if (-not (Test-Path -LiteralPath $deployedArtifact -PathType Leaf)) {
        throw 'Simulated -IncludeConfig deploy omitted the SSS patch artifact.'
    }
    $deployedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $deployedArtifact).Hash.ToLowerInvariant()
    if ($deployedHash -ne $expectedSha256) {
        throw "Simulated deployed SSS patch artifact SHA-256 mismatch: $deployedHash."
    }
} finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'PASS: SSS DXBC patch devkit deploy contract'
