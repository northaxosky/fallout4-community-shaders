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

    Where an entry pins `cb_read_counts` and is marked `body_reconstructed`, it
    compares how many times each of those registers is read, which is finer
    still. An entry may opt out of that last check only by carrying the single
    macro named in `scope.count_exemption_axis` and giving a reason, so an
    unreconstructed axis stays visible and countable instead of becoming a
    free-form exemption list.

    Where entries pin `immediate_constant_vectors`, it also compares how many
    float4 rows the compiled body declares in its immediate constant buffer.
    That pin is all-or-none across a manifest: a family either measured it for
    every entry or for none, so it cannot be dropped from the one entry it would
    have caught. A manifest written before the pin existed simply omits it and
    behaves exactly as it did.

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

# Reflected constant-buffer sizes, read from the RDEF block of the same listing.
#
# This is deliberately a SECOND opinion on a size the declaration check already
# compares, because fxc derives the two independently: the SHEX
# `dcl_constantbuffer` size comes from the highest register the body READS,
# while the RDEF size comes from what the source DECLARES. A trailing member
# that is declared unconditionally but read by only some permutations is
# therefore invisible in the instruction stream and still oversizes the buffer
# a host reflects. Native blobs are stripped, so their expected size is the one
# the manifest already pins from SHEX - and that is exactly what makes checking
# it against RDEF meaningful rather than circular.
function Get-ReflectedCbSizes([string[]]$Listing) {
    $sizes = @{}
    $current = $null
    foreach ($line in $Listing) {
        if ($line -match '^//\s+cbuffer\s+\S*CB(\d+)\s*$') {
            $current = "b$($Matches[1])"
        } elseif ($line -match '^//\s*\}\s*$') {
            $current = $null
        } elseif ($current -and $line -match 'Offset:\s+(\d+)\s+Size:\s+(\d+)') {
            $end = ([int]$Matches[1] + [int]$Matches[2]) / 16
            if (-not $sizes.ContainsKey($current) -or $end -gt $sizes[$current]) {
                $sizes[$current] = $end
            }
        }
    }
    return $sizes
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

# How many times the body reads each constant register. This is the finest of
# the three pins. It is sensitive to whether a body is reconstructed at all,
# so an entry may only
# opt out of it along the single named axis the manifest declares.
function Get-ConstantReadCounts([string[]]$Listing) {
    $counts = [Collections.Specialized.OrderedDictionary]::new()
    foreach ($line in $Listing) {
        $text = $line.Trim()
        if (-not $text -or $text.StartsWith('dcl_') -or $text.StartsWith('//')) { continue }
        foreach ($match in [regex]::Matches($text, '\bcb(\d+)\[(\d+)\]')) {
            $key = "cb$($match.Groups[1].Value)[$($match.Groups[2].Value)]"
            if ($counts.Contains($key)) { $counts[$key] = [int]$counts[$key] + 1 }
            else { $counts[$key] = 1 }
        }
    }
    $parts = foreach ($key in ($counts.Keys | Sort-Object)) { "$key=$($counts[$key])" }
    return ($parts -join ' ')
}

function ConvertTo-ExpectedReadCounts($CbReadCounts) {
    $parts = foreach ($property in ($CbReadCounts.PSObject.Properties | Sort-Object Name)) {
        "$($property.Name)=$($property.Value)"
    }
    return ($parts -join ' ')
}

# How many float4 rows the body declares as its immediate constant buffer. The
# block starts on the `dcl_immediateConstantBuffer` line itself - fxc puts the
# first row there - and runs until the next declaration, so the scan is bounded
# by `dcl_` rather than by a row count the listing never states.
function Get-ImmediateConstantVectorCount([string[]]$Listing) {
    $count = 0
    $inBlock = $false
    foreach ($line in $Listing) {
        $text = $line.Trim()
        if (-not $inBlock) {
            if ($text -match '^dcl_immediateConstantBuffer') { $inBlock = $true } else { continue }
        } elseif ($text -match '^dcl_') {
            break
        }
        $count += [regex]::Matches($text, '\{\s*[-+]?(?:\d|\.\d)').Count
    }
    return $count
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
$countExemptionAxis = Get-OptionalMember $manifest.scope 'count_exemption_axis'

# The immediate-constant-vector pin is all-or-none, so a family that measured it
# cannot quietly drop it from the one entry whose ICB would have differed.
$icbPinned = @($manifest.entries |
    Where-Object { $null -ne (Get-OptionalMember $_ 'immediate_constant_vectors') }).Count
if ($icbPinned -gt 0 -and $icbPinned -ne $manifest.entries.Count) {
    Exit-WithError ("immediate_constant_vectors is pinned on $icbPinned of " +
        "$($manifest.entries.Count) entries; it is all-or-none") 2
}

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
$countChecked = 0
$countExempt = 0
$icbChecked = 0
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

        # Same sizes again, from reflection rather than the instruction stream.
        $reflected = Get-ReflectedCbSizes (Get-Content -LiteralPath $result.Listing)
        foreach ($cbProperty in $group.Value.cb.PSObject.Properties) {
            $wantSize = [int]$cbProperty.Value[0]
            $gotSize = 0
            if ($reflected.ContainsKey($cbProperty.Name)) { $gotSize = [int]$reflected[$cbProperty.Name] }
            if ($gotSize -ne $wantSize) {
                $failures += ("$tag`: reflected $($cbProperty.Name) size differs from the native blob" +
                    "`n    native   : $wantSize" +
                    "`n    reflected: $gotSize")
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
        $pinnedCounts = Get-OptionalMember $entry 'cb_read_counts'
        $reconstructed = Get-OptionalMember $entry 'body_reconstructed'
        if ($null -eq $reconstructed) { $reconstructed = $true }
        $pinnedIcb = Get-OptionalMember $entry 'immediate_constant_vectors'
        if ($null -ne $pinnedIcb) {
            ++$icbChecked
            $actualIcb = Get-ImmediateConstantVectorCount (Get-Content -LiteralPath $result.Listing)
            if ($actualIcb -ne [int]$pinnedIcb) {
                $failures += ("$tag`: immediate constant-buffer vector count differs from the native blob" +
                    "`n    native: $([int]$pinnedIcb)" +
                    "`n    ours  : $actualIcb")
            }
        }
        if ($pinnedCounts -and $reconstructed) {
            ++$countChecked
            $actualCounts = Get-ConstantReadCounts (Get-Content -LiteralPath $result.Listing)
            $expectedCounts = ConvertTo-ExpectedReadCounts $pinnedCounts
            if ($actualCounts -cne $expectedCounts) {
                $failures += ("$tag`: constant read-counts differ from the native blob" +
                    "`n    native: $expectedCounts" +
                    "`n    ours  : $actualCounts")
            }
        } elseif ($pinnedCounts) {
            # An entry may only skip the read-count check by carrying the one
            # axis the manifest names as unreconstructed. Without this, the
            # exemption would be a free-form list anyone could quietly widen.
            ++$countExempt
            if (-not $countExemptionAxis) {
                $failures += "$tag`: body_reconstructed is false but the manifest declares no count_exemption_axis"
            } elseif (@($entry.defines) -notcontains $countExemptionAxis -and
                      @($entry.defines) -notcontains "$countExemptionAxis=1") {
                $failures += ("$tag`: exempt from the read-count check without carrying " +
                    "$countExemptionAxis`n    defines: $(@($entry.defines) -join ' ')")
            } elseif (-not (Get-OptionalMember $entry 'count_exempt_reason')) {
                $failures += "$tag`: exempt from the read-count check with no count_exempt_reason"
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
if (($countChecked + $countExempt) -ne $checked) {
    Exit-WithError ("read-count pin missing: $($countChecked + $countExempt) of $checked entries carry cb_read_counts")
}
if ($icbPinned -gt 0 -and $icbChecked -ne $checked) {
    Exit-WithError ("immediate-constant-vector pin missing: $icbChecked of $checked entries were checked")
}
if ($failures.Count -gt 0) {
    foreach ($failure in $failures) { Write-Host "ERROR: $failure" }
    Exit-WithError "$($failures.Count) $reportLabel admission check(s) failed"
}

$countNote = "$countChecked / $checked also match its read-counts"
if ($countExempt -gt 0) {
    $countNote += " ($countExempt exempt: $countExemptionAxis is unreconstructed)"
}
if ($icbPinned -gt 0) {
    $countNote += " and $icbChecked / $checked its immediate constant-buffer vector count"
}
Write-Host ("PASS: $checked / $checked $reportLabel permutations match the native ABI " +
    "and constant read-set, $countNote; " +
    "$rejectedChecked / $rejectedChecked rejected macro sets still refuse to compile; " +
    "$compileOnlyChecked / $compileOnlyChecked $compileOnlyLabel still compile.")
exit 0
