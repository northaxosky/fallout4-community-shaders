#Requires -Version 5.1
<#
.SYNOPSIS
    Generate compile_commands.json for clangd-based editors.

.EXAMPLE
    pwsh scripts\gen-compile-commands.ps1
#>
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

Push-Location $repoRoot
try {
    xmake project -k compile_commands
    if ($LASTEXITCODE -ne 0) {
        throw "xmake project -k compile_commands failed (exit $LASTEXITCODE)."
    }
} finally {
    Pop-Location
}

Write-Host "OK: $(Join-Path $repoRoot 'compile_commands.json')" -ForegroundColor Green
Write-Host "clangd reloads it automatically; no editor restart needed." -ForegroundColor Green
