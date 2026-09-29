<#
.SYNOPSIS
	Formats tracked C++ and HLSL sources with the pinned clang-format.

.DESCRIPTION
	Installs the pinned clang-format wheel into build/tools on first use and applies the
	repository .clang-format to every tracked C, C++ and HLSL source outside extern/.

.PARAMETER Check
	Report unformatted files and fail instead of rewriting them.
#>
[CmdletBinding()]
param(
	[switch]$Check
)

$ErrorActionPreference = 'Stop'
$version = '22.1.4'
$root = Split-Path -Parent $PSScriptRoot
$toolDirectory = Join-Path $root "build/tools/clang-format-$version"
$executable = Join-Path $toolDirectory ($IsWindows ? 'clang_format/data/bin/clang-format.exe' : 'clang_format/data/bin/clang-format')

if (-not (Test-Path $executable)) {
	& ($IsWindows ? 'python' : 'python3') -m pip install --quiet --disable-pip-version-check --target $toolDirectory "clang-format==$version"
	if ($LASTEXITCODE -ne 0) { throw "Cannot install clang-format $version" }
}

Push-Location $root
try {
	$files = @(git ls-files '*.h' '*.hpp' '*.c' '*.cpp' '*.hlsl' '*.hlsli' ':!:extern/**' ':!:include/**')
	$mode = $Check ? @('--dry-run', '--Werror') : @('-i')
	# Heavily preprocessor-branched shaders take minutes each, so run files in parallel.
	$failed = $files | ForEach-Object -ThrottleLimit ([Environment]::ProcessorCount) -Parallel {
		$arguments = $using:mode
		& $using:executable --style=file @arguments $_
		if ($LASTEXITCODE -ne 0) { $_ }
	}
	if ($failed) { throw "Sources are not clang-formatted; run scripts/format.ps1:`n$($failed -join "`n")" }
} finally {
	Pop-Location
}
