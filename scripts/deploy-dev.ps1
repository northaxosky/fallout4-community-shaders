<#
.SYNOPSIS
	Builds the plugin and deploys it into an MO2 mod folder with a chosen settings TOML.

.DESCRIPTION
	Clears the deploy environment variables, refuses to run while the game is open, builds releasedbg,
	installs into build/isolated-install, mirrors that into the mod folder (keeping its ShaderCache,
	Presets and plugin TOML), then overwrites the plugin TOML and verifies the DLL and PDB hashes.
	Use -WhatIf to list the steps without building or copying.

.PARAMETER Mod
	MO2 mod folder path, or a folder name under -ModsRoot.

.PARAMETER Toml
	Settings TOML to deploy, typically written for the feature under test.

.PARAMETER Preset
	Base preset from scripts/dev-presets (taa-all, dlss-all) used when -Toml is not given.

.PARAMETER ModsRoot
	Folder holding named mods.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
	[string]$Mod = 'Community Shaders - Dev',
	[string]$Toml,
	[string]$Preset = 'taa-all',
	[string]$ModsRoot = 'C:\Games\Modding\Nucleus\mods'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$plugin = 'F4SE\Plugins\FO4CommunityShaders'
$install = Join-Path $root 'build\isolated-install'

if (-not $Toml) { $Toml = Join-Path $PSScriptRoot "dev-presets\$Preset.toml" }
if (-not (Test-Path -LiteralPath $Toml -PathType Leaf)) { throw "Settings TOML not found: $Toml" }
$modPath = (Test-Path -LiteralPath $Mod -PathType Container) ? (Resolve-Path -LiteralPath $Mod).Path : (Join-Path $ModsRoot $Mod)
if (-not (Test-Path -LiteralPath $modPath -PathType Container)) { throw "Mod folder not found: $modPath" }

Remove-Item Env:\XSE_FO4_MODS_PATH, Env:\XSE_FO4_GAME_PATH -ErrorAction SilentlyContinue -WhatIf:$false
if (Get-Process Fallout4, f4se_loader -ErrorAction SilentlyContinue) { throw 'Close Fallout 4 before deploying.' }

if ($PSCmdlet.ShouldProcess($modPath, "build, install and deploy with $Toml")) {
	if (Test-Path -LiteralPath $install) { Remove-Item -LiteralPath $install -Recurse -Force }
	foreach ($step in @(@('f', '-P', $root, '-m', 'releasedbg', '-y'), @('build', '-P', $root, '-y', 'FO4CommunityShaders'), @('install', '-P', $root, '-o', $install, 'FO4CommunityShaders'))) {
		& xmake @step
		if ($LASTEXITCODE -ne 0) { throw "xmake $($step -join ' ') failed (exit $LASTEXITCODE)." }
	}
	robocopy $install $modPath /MIR /NJH /NJS /NP /NDL /XF FO4CommunityShaders.toml meta.ini /XD ShaderCache Presets | Out-Null
	if ($LASTEXITCODE -ge 8) { throw "robocopy failed (exit $LASTEXITCODE)." }
	New-Item -ItemType Directory -Force (Join-Path $modPath $plugin) | Out-Null
	Copy-Item -LiteralPath $Toml -Destination (Join-Path $modPath "$plugin\FO4CommunityShaders.toml") -Force

	foreach ($name in 'FO4CommunityShaders.dll', 'FO4CommunityShaders.pdb') {
		$built = (Get-FileHash (Join-Path $install "F4SE\Plugins\$name") -Algorithm SHA256).Hash
		$deployed = (Get-FileHash (Join-Path $modPath "F4SE\Plugins\$name") -Algorithm SHA256).Hash
		if ($built -ne $deployed) { throw "$name hash mismatch after deploy." }
		if ($name.EndsWith('.dll')) { $dll = $deployed }
	}
	$dirty = git -C $root status --porcelain --untracked-files=no
	"Deployed $(git -C $root rev-parse --short HEAD)$($dirty ? '+dirty' : '') DLL SHA-256 $dll -> $modPath"
}
