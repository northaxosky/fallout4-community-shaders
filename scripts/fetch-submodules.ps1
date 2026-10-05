<#
.SYNOPSIS
	Fetches FO4 dependencies and LFS assets without the shared repository's Skyrim dependencies.

.DESCRIPTION
	In a linked worktree, each submodule that already exists in the main checkout's object store is
	cloned with --reference --dissociate, so only missing objects come from the network and the
	worktree keeps its own copy. Streamline is not recursed: its nested FidelityFX-SDK is unused
	(the build needs only its in-tree headers; runtime DLLs come from fetch-sdks.ps1).
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

function Initialize-Submodule {
	param(
		[string]$RepoDir,
		[string]$Path,
		[string]$ReferenceDir,
		[switch]$Recurse,
		[int]$Depth
	)

	$arguments = @('-C', $RepoDir, 'submodule', 'update', '--init')
	if ($Depth) { $arguments += @('--depth', $Depth) }
	# Git refuses a shallow reference; a depth-1 network clone is cheap anyway.
	if ($ReferenceDir -and (Test-Path -LiteralPath $ReferenceDir) -and
		(& git -C $ReferenceDir rev-parse --is-shallow-repository) -eq 'false') {
		$arguments += @('--reference', $ReferenceDir, '--dissociate')
	}
	Invoke-Git ($arguments + @('--', $Path))

	if (-not $Recurse) { return }
	# --reference applies to one level, so nested modules are initialized one by one.
	$child = Join-Path $RepoDir $Path
	if (-not (Test-Path -LiteralPath (Join-Path $child '.gitmodules'))) { return }
	$entries = & git -C $child config --file .gitmodules --get-regexp '^submodule\..*\.path$'
	if ($LASTEXITCODE -ne 0) { return }
	foreach ($entry in $entries) {
		$nested = ($entry -split ' ', 2)[1]
		$nestedReference = if ($ReferenceDir) { Join-Path $ReferenceDir "modules/$nested" }
		Initialize-Submodule -RepoDir $child -Path $nested -ReferenceDir $nestedReference -Recurse
	}
}

$recursiveSubmodules = @('extern/CommonLibF4')
$lfsCommand = 'git lfs install --local && git lfs pull'

Push-Location (Split-Path -Parent $PSScriptRoot)
try {
	$gitDir = (& git rev-parse --path-format=absolute --git-dir)
	$commonDir = (& git rev-parse --path-format=absolute --git-common-dir)
	$modulesRoot = if ($gitDir -ne $commonDir) { Join-Path $commonDir 'modules' }
	function Get-ModuleReference([string]$Path) {
		if ($modulesRoot) { Join-Path $modulesRoot $Path }
	}

	# Git has no per-submodule update-recursion policy. Never recurse into the shared repo.
	$shared = 'extern/community-shaders-shared'
	Initialize-Submodule -RepoDir . -Path $shared -ReferenceDir (Get-ModuleReference $shared) -Depth 1
	Initialize-Submodule -RepoDir . -Path 'extern/Streamline' `
		-ReferenceDir (Get-ModuleReference 'extern/Streamline')
	foreach ($path in $recursiveSubmodules) {
		Initialize-Submodule -RepoDir . -Path $path -ReferenceDir (Get-ModuleReference $path) -Recurse
	}

	Invoke-Git @('lfs', 'install', '--local')
	Invoke-Git @('lfs', 'pull')
	Invoke-Git @('submodule', 'foreach', $lfsCommand)
	foreach ($path in $recursiveSubmodules) {
		Invoke-Git @('-C', $path, 'submodule', 'foreach', '--recursive', $lfsCommand)
	}
} finally {
	Pop-Location
}
