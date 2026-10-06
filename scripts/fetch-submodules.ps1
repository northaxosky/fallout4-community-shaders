<#
.SYNOPSIS
	Fetches FO4 dependencies and LFS assets without the shared repository's Skyrim dependencies.

.DESCRIPTION
	Streamline is not recursed: its nested FidelityFX-SDK is unused (the build needs only its in-tree
	headers; runtime DLLs come from fetch-sdks.ps1).
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Invoke-Git {
	param([string[]]$Arguments)

	& git @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "git $($Arguments -join ' ') failed (exit $LASTEXITCODE)."
	}
}

$recursiveSubmodules = @('extern/CommonLibF4')
$lfsCommand = 'git lfs install --local && git lfs pull'

Push-Location (Split-Path -Parent $PSScriptRoot)
try {
	# Git has no per-submodule update-recursion policy. Never recurse into the shared repo.
	Invoke-Git @('submodule', 'update', '--init', '--depth', '1', '--', 'extern/community-shaders-shared')
	Invoke-Git @('submodule', 'update', '--init', '--', 'extern/Streamline')
	Invoke-Git (@('submodule', 'update', '--init', '--recursive', '--') + $recursiveSubmodules)

	Invoke-Git @('lfs', 'install', '--local')
	Invoke-Git @('lfs', 'pull')
	Invoke-Git @('submodule', 'foreach', $lfsCommand)
	foreach ($path in $recursiveSubmodules) {
		Invoke-Git @('-C', $path, 'submodule', 'foreach', '--recursive', $lfsCommand)
	}
} finally {
	Pop-Location
}
