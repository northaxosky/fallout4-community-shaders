<#
.SYNOPSIS
    Fail-closed check that a reconstructed native macro family still declares the
    ABI measured from the archive blobs it was reconstructed from.

.DESCRIPTION
    Compiles every macro set listed in the evidence manifest against the source
    the manifest names, parses the declaration block out of the fxc listing, and
    compares it to the declaration set measured from that blob in the archive:
    constant-buffer sizes and indexing mode, SRV slots and resource types,
    sampler slots and modes, and the input/output signature. `dcl_temps` is
    excluded on purpose - it is register allocation, not ABI.

    Where an entry pins `cb_reads`, it also compares which constant-buffer
    registers the compiled body actually reads. That is finer than the
    declaration set and catches a dropped or invented constant read that
    identical declarations would otherwise hide.

    It also asserts that the macro sets in the `rejected` list still fail to
    compile, so the admission cannot silently widen, and that the `compile_only`
    list still compiles, so the fail-closed guards cannot over-reach and lock
    out a legitimate macro set.

    The pinned values come from the game bytecode, so this cannot bless its own
    output. It is an ABI claim only; the manifest's `scope.note` records what
    the family does and does not prove.

    This verifier is family-agnostic: everything it reports comes from the
    manifest, so a new native family needs a new evidence file and a new test
    registration, not a new copy of this script. `verify-pointomni-admission.ps1`
    predates it and stays as it is.

.PARAMETER FxcPath
    Override fxc.exe. FXC_PATH is used when this parameter is omitted.

.PARAMETER ManifestPath
    The evidence file. Defaults to the DIRSPLITS=2 directional family.

.PARAMETER RepoRoot
    Override the repository root used to resolve the shader source.
#>
[CmdletBinding()]
param(
    [string]$FxcPath,
    [string]$ManifestPath,
    [string]$RepoRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $RepoRoot) {
    $RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
}
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $PSScriptRoot 'dirsplits2-native-abi.json'
}
if (-not $FxcPath) {
    $FxcPath = if ($env:FXC_PATH) {
        $env:FXC_PATH
    } else {
        'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe'
    }
}

function Exit-WithError([string]$Message, [int]$Code = 1) {
    Write-Host "FAIL: $Message"
    exit $Code
}

function Get-OptionalMember($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($property) { return $property.Value }
    return $null
}

# Reduce an fxc listing to the declaration set that defines the ABI.
function Get-DeclaredContract([string[]]$Listing) {
    $cb = [ordered]@{}; $srv = [ordered]@{}; $sampler = [ordered]@{}; $sig = @()
    foreach ($line in $Listing) {
        $text = $line.Trim()
        if ($text -match '^dcl_constantbuffer CB(\d+)\[(\d+)\], (\w+)') {
            $cb["b$($Matches[1])"] = "$($Matches[2]):$($Matches[3])"
        } elseif ($text -match '^dcl_sampler s(\d+), (\w+)') {
            $sampler["s$($Matches[1])"] = $Matches[2]
        } elseif ($text -match '^dcl_resource_(\w+) \([^)]*\) t(\d+)') {
            $srv["t$($Matches[2])"] = $Matches[1]
        } elseif ($text -match '^dcl_(output|input)') {
            $sig += $text
        }
    }
    return [pscustomobject]@{
        cb      = ($cb.Keys      | Sort-Object | ForEach-Object { "$_=$($cb[$_])" })      -join ','
        srv     = ($srv.Keys     | Sort-Object | ForEach-Object { "$_=$($srv[$_])" })     -join ','
        sampler = ($sampler.Keys | Sort-Object | ForEach-Object { "$_=$($sampler[$_])" }) -join ','
        sig     = (($sig | Sort-Object) -join ' | ')
    }
}

# Flatten a JSON abi_groups member into the same comparable shape.
function ConvertTo-ExpectedContract($Group) {
    $cb = $Group.cb.PSObject.Properties | Sort-Object Name |
        ForEach-Object { "$($_.Name)=$($_.Value[0]):$($_.Value[1])" }
    $srv = $Group.srv.PSObject.Properties | Sort-Object Name |
        ForEach-Object { "$($_.Name)=$($_.Value)" }
    $sampler = $Group.sampler.PSObject.Properties | Sort-Object Name |
        ForEach-Object { "$($_.Name)=$($_.Value)" }
    return [pscustomobject]@{
        cb      = $cb -join ','
        srv     = $srv -join ','
        sampler = $sampler -join ','
        sig     = ((@($Group.sig) | Sort-Object) -join ' | ')
    }
}

# Which constant-buffer registers the body actually reads. This is finer than
# the declaration set: two macro sets can declare the same CB and still read a
# different span of it, so pinning this catches a dropped or invented constant
# read that the declarations alone would hide.
function Get-ConstantReadSet([string[]]$Listing) {
    $reads = @{}
    foreach ($line in $Listing) {
        $text = $line.Trim()
        if (-not $text -or $text.StartsWith('dcl_') -or $text.StartsWith('//')) { continue }
        foreach ($match in [regex]::Matches($text, '\bcb(\d+)\[(\d+)\]')) {
            $key = "cb$($match.Groups[1].Value)"
            if (-not $reads.ContainsKey($key)) {
                $reads[$key] = [Collections.Generic.HashSet[int]]::new()
            }
            [void]$reads[$key].Add([int]$match.Groups[2].Value)
        }
    }
    $parts = foreach ($key in ($reads.Keys | Sort-Object)) {
        "$key=[" + ((@($reads[$key]) | Sort-Object) -join ',') + ']'
    }
    return ($parts -join ' ')
}

function ConvertTo-ExpectedReadSet($CbReads) {
    $parts = foreach ($property in ($CbReads.PSObject.Properties | Sort-Object Name)) {
        "$($property.Name)=[" + ((@($property.Value) | Sort-Object) -join ',') + ']'
    }
    return ($parts -join ' ')
}

if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    Exit-WithError "evidence file not found: $ManifestPath" 2
}
try {
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
} catch {
    Exit-WithError "evidence file is not valid JSON: $($_.Exception.Message)" 2
}
if ($manifest.schema -cne 'fo4cs.native-abi-admission' -or $manifest.schema_version -ne 1) {
    Exit-WithError 'unexpected evidence schema' 2
}

$reportLabel = Get-OptionalMember $manifest.scope 'report_label'
if (-not $reportLabel) { $reportLabel = $manifest.scope.family }
$compileOnlyLabel = Get-OptionalMember $manifest.scope 'compile_only_label'
if (-not $compileOnlyLabel) { $compileOnlyLabel = 'compile-only' }

$fxcCommand = $null
if (Test-Path -LiteralPath $FxcPath -PathType Leaf) {
    $fxcCommand = (Resolve-Path -LiteralPath $FxcPath).Path
} else {
    $command = Get-Command $FxcPath -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { $fxcCommand = $command.Source }
}
if (-not $fxcCommand) { Exit-WithError "fxc not found: $FxcPath" 2 }

$sourcePath = [IO.Path]::GetFullPath(
    (Join-Path $RepoRoot ($manifest.source.Replace('/', [IO.Path]::DirectorySeparatorChar))))
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    Exit-WithError "shader source not found: $sourcePath" 2
}
$includePath = Split-Path -Parent $sourcePath

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("fo4cs-abi-" + [Guid]::NewGuid().ToString('n'))
$failures = @()
$checked = 0
$rejectedChecked = 0
$compileOnlyChecked = 0
try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

    function Invoke-Fxc([string[]]$Defines, [string]$Tag) {
        $listing = Join-Path $tempRoot "$Tag.asm"
        $arguments = @('/T', $manifest.profile, '/E', $manifest.compiler.entry_point,
            '/nologo', '/O3', '/I', $includePath)
        foreach ($define in $Defines) {
            $arguments += @('/D', ($(if ($define -like '*=*') { $define } else { "$define=1" })))
        }
        $arguments += @('/Fc', $listing, $sourcePath)
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $output = & $fxcCommand @arguments 2>&1
            $code = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previous
        }
        return [pscustomobject]@{
            ExitCode = $code
            Output   = ($output | Out-String).Trim()
            Listing  = $listing
        }
    }

    $index = 0
    foreach ($entry in $manifest.entries) {
        ++$index
        ++$checked
        $tag = $entry.native_blob_sha1.Substring(0, 8)
        $group = $manifest.abi_groups.PSObject.Properties[$entry.abi_group]
        if (-not $group) {
            $failures += "$tag`: unknown abi_group '$($entry.abi_group)'"
            continue
        }
        $result = Invoke-Fxc (@($manifest.common_defines) + @($entry.defines)) "entry$index"
        if ($result.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $result.Listing)) {
            $failures += "$tag`: compile failed`n$($result.Output)"
            continue
        }
        $actual = Get-DeclaredContract (Get-Content -LiteralPath $result.Listing)
        $expected = ConvertTo-ExpectedContract $group.Value
        foreach ($field in 'cb', 'srv', 'sampler', 'sig') {
            if ($actual.$field -cne $expected.$field) {
                $failures += ("$tag`: $field differs from the native blob" +
                    "`n    native: $($expected.$field)" +
                    "`n    ours  : $($actual.$field)")
            }
        }
        $pinnedReads = Get-OptionalMember $entry 'cb_reads'
        if ($pinnedReads) {
            $actualReads = Get-ConstantReadSet (Get-Content -LiteralPath $result.Listing)
            $expectedReads = ConvertTo-ExpectedReadSet $pinnedReads
            if ($actualReads -cne $expectedReads) {
                $failures += ("$tag`: constant read-set differs from the native blob" +
                    "`n    native: $expectedReads" +
                    "`n    ours  : $actualReads")
            }
        }
    }

    $index = 0
    foreach ($case in $manifest.rejected) {
        ++$index
        ++$rejectedChecked
        # Rejected cases are self-contained: they carry every macro they need,
        # because some of them reject an otherwise-valid set for one missing or
        # one extra macro.
        $result = Invoke-Fxc @($case.defines) "rejected$index"
        if ($result.ExitCode -eq 0) {
            $failures += "rejected case compiled but must not: $($case.reason)"
        }
    }

    $index = 0
    foreach ($case in $manifest.compile_only) {
        ++$index
        ++$compileOnlyChecked
        $label = Get-OptionalMember $case 'native_blob_sha1'
        if ($label) { $label = $label.Substring(0, 8) } else { $label = ($case.defines -join ' ') }
        $result = Invoke-Fxc (@($manifest.common_defines) + @($case.defines)) "compileonly$index"
        if ($result.ExitCode -ne 0) {
            $failures += ("compile-only case must still compile: $label`n$($result.Output)")
        }
    }
} finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($checked -ne $manifest.entries.Count -or $checked -eq 0) {
    Exit-WithError "checked $checked of $($manifest.entries.Count) admitted permutations"
}
if ($rejectedChecked -ne $manifest.rejected.Count) {
    Exit-WithError "checked $rejectedChecked of $($manifest.rejected.Count) rejected cases"
}
if ($compileOnlyChecked -ne $manifest.compile_only.Count) {
    Exit-WithError "checked $compileOnlyChecked of $($manifest.compile_only.Count) compile-only cases"
}
if ($failures.Count -gt 0) {
    foreach ($failure in $failures) { Write-Host "ERROR: $failure" }
    Exit-WithError "$($failures.Count) $reportLabel admission check(s) failed"
}

Write-Host ("PASS: $checked / $checked $reportLabel permutations match the native ABI " +
    "and constant read-set; " +
    "$rejectedChecked / $rejectedChecked rejected macro sets still refuse to compile; " +
    "$compileOnlyChecked / $compileOnlyChecked $compileOnlyLabel still compile.")
exit 0
