#Requires -Version 5.1
<#
.SYNOPSIS
    Verify the shader fidelity conformance manifest and its DXBC baselines.

.PARAMETER FxcPath
    Override fxc.exe. FXC_PATH is used when this parameter is omitted.

.PARAMETER ManifestPath
    Override the conformance manifest path for testing.

.PARAMETER RepoRoot
    Override the repository root for testing.
#>
[CmdletBinding()]
param(
    [string]$FxcPath,
    [string]$ManifestPath,
    [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $scriptDir 'shader-fidelity-conformance.json'
}
if ([string]::IsNullOrWhiteSpace($FxcPath)) {
    $FxcPath = if ($env:FXC_PATH) {
        $env:FXC_PATH
    } else {
        'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe'
    }
}

function Exit-WithError {
    param([string]$Message, [int]$Code)
    [Console]::Error.WriteLine("ERROR: $Message")
    exit $Code
}

function Add-ValidationError {
    param(
        [System.Collections.Generic.List[string]]$Errors,
        [string]$Message
    )
    $Errors.Add($Message)
}

function Get-LowerFileHash {
    param([string]$Path, [ValidateSet('SHA1', 'SHA256')][string]$Algorithm)

    $stream = [IO.File]::OpenRead($Path)
    $hasher = if ($Algorithm -eq 'SHA1') {
        [Security.Cryptography.SHA1]::Create()
    } else {
        [Security.Cryptography.SHA256]::Create()
    }
    try {
        return ([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $hasher.Dispose()
        $stream.Dispose()
    }
}

function Test-ExactFields {
    param(
        [object]$Value,
        [string[]]$Expected,
        [string]$Context,
        [System.Collections.Generic.List[string]]$Errors
    )

    if ($null -eq $Value -or $Value -is [System.Array] -or $Value -is [string] -or
        $Value -is [ValueType]) {
        Add-ValidationError $Errors "$Context must be an object"
        return $false
    }

    $actual = @($Value.PSObject.Properties.Name)
    $matches = $actual.Count -eq $Expected.Count
    if ($matches) {
        for ($i = 0; $i -lt $Expected.Count; $i++) {
            if ($actual[$i] -cne $Expected[$i]) {
                $matches = $false
                break
            }
        }
    }
    if (-not $matches) {
        Add-ValidationError $Errors ("{0} fields must be exactly, in order: {1}; found: {2}" -f
            $Context, ($Expected -join ', '), ($actual -join ', '))
    }
    return $matches
}

function Test-Integer {
    param([object]$Value)
    return $Value -is [byte] -or $Value -is [sbyte] -or
        $Value -is [int16] -or $Value -is [uint16] -or
        $Value -is [int32] -or $Value -is [uint32] -or
        $Value -is [int64] -or $Value -is [uint64]
}

function Skip-JsonWhitespace {
    param([string]$Text, [ref]$Index)
    while ($Index.Value -lt $Text.Length) {
        $character = $Text[$Index.Value]
        if ($character -ne ' ' -and $character -ne "`t" -and
            $character -ne "`r" -and $character -ne "`n") {
            break
        }
        $Index.Value++
    }
}

function Read-JsonString {
    param([string]$Text, [ref]$Index)
    if ($Index.Value -ge $Text.Length -or $Text[$Index.Value] -ne '"') {
        throw "expected JSON string at offset $($Index.Value)"
    }
    $Index.Value++
    $builder = New-Object Text.StringBuilder
    while ($Index.Value -lt $Text.Length) {
        $character = $Text[$Index.Value++]
        if ($character -eq '"') {
            return $builder.ToString()
        }
        if ([int]$character -lt 0x20) {
            throw "unescaped control character in JSON string at offset $($Index.Value - 1)"
        }
        if ($character -ne '\') {
            [void]$builder.Append($character)
            continue
        }
        if ($Index.Value -ge $Text.Length) {
            throw 'unterminated JSON escape'
        }

        $escape = $Text[$Index.Value++]
        switch ($escape) {
            '"'  { [void]$builder.Append('"'); continue }
            '\'  { [void]$builder.Append('\'); continue }
            '/'  { [void]$builder.Append('/'); continue }
            'b'  { [void]$builder.Append([char]8); continue }
            'f'  { [void]$builder.Append([char]12); continue }
            'n'  { [void]$builder.Append("`n"); continue }
            'r'  { [void]$builder.Append("`r"); continue }
            't'  { [void]$builder.Append("`t"); continue }
            'u'  {
                if ($Index.Value + 4 -gt $Text.Length) {
                    throw 'truncated JSON Unicode escape'
                }
                $hex = $Text.Substring($Index.Value, 4)
                if ($hex -cnotmatch '^[0-9A-Fa-f]{4}$') {
                    throw "invalid JSON Unicode escape at offset $($Index.Value)"
                }
                $codePoint = [int]::Parse(
                    $hex,
                    [Globalization.NumberStyles]::HexNumber,
                    [Globalization.CultureInfo]::InvariantCulture)
                $Index.Value += 4
                if ($codePoint -ge 0xD800 -and $codePoint -le 0xDBFF) {
                    if ($Index.Value + 6 -gt $Text.Length -or
                        $Text[$Index.Value] -ne '\' -or $Text[$Index.Value + 1] -ne 'u') {
                        throw 'high surrogate without a low surrogate in JSON string'
                    }
                    $lowHex = $Text.Substring($Index.Value + 2, 4)
                    if ($lowHex -cnotmatch '^[0-9A-Fa-f]{4}$') {
                        throw "invalid JSON Unicode escape at offset $($Index.Value + 2)"
                    }
                    $low = [int]::Parse(
                        $lowHex,
                        [Globalization.NumberStyles]::HexNumber,
                        [Globalization.CultureInfo]::InvariantCulture)
                    if ($low -lt 0xDC00 -or $low -gt 0xDFFF) {
                        throw 'high surrogate without a low surrogate in JSON string'
                    }
                    $Index.Value += 6
                    $scalar = 0x10000 + (($codePoint - 0xD800) * 0x400) + ($low - 0xDC00)
                    [void]$builder.Append([char]::ConvertFromUtf32($scalar))
                } elseif ($codePoint -ge 0xDC00 -and $codePoint -le 0xDFFF) {
                    throw 'low surrogate without a high surrogate in JSON string'
                } else {
                    [void]$builder.Append([char]$codePoint)
                }
                continue
            }
            default { throw "invalid JSON escape at offset $($Index.Value - 1)" }
        }
    }
    throw 'unterminated JSON string'
}

function Test-JsonDigit {
    param([char]$Character)
    $value = [int]$Character
    return $value -ge [int][char]'0' -and $value -le [int][char]'9'
}

function Read-JsonNumber {
    param([string]$Text, [ref]$Index)
    if ($Text[$Index.Value] -eq '-') {
        $Index.Value++
        if ($Index.Value -ge $Text.Length) { throw 'truncated JSON number' }
    }
    if ($Text[$Index.Value] -eq '0') {
        $Index.Value++
        if ($Index.Value -lt $Text.Length -and (Test-JsonDigit $Text[$Index.Value])) {
            throw "leading zero in JSON number at offset $($Index.Value)"
        }
    } elseif ($Text[$Index.Value] -ge '1' -and $Text[$Index.Value] -le '9') {
        do { $Index.Value++ }
        while ($Index.Value -lt $Text.Length -and (Test-JsonDigit $Text[$Index.Value]))
    } else {
        throw "invalid JSON number at offset $($Index.Value)"
    }
    if ($Index.Value -lt $Text.Length -and $Text[$Index.Value] -eq '.') {
        $Index.Value++
        if ($Index.Value -ge $Text.Length -or -not (Test-JsonDigit $Text[$Index.Value])) {
            throw "invalid JSON fraction at offset $($Index.Value)"
        }
        while ($Index.Value -lt $Text.Length -and (Test-JsonDigit $Text[$Index.Value])) {
            $Index.Value++
        }
    }
    if ($Index.Value -lt $Text.Length -and
        ($Text[$Index.Value] -eq 'e' -or $Text[$Index.Value] -eq 'E')) {
        $Index.Value++
        if ($Index.Value -lt $Text.Length -and
            ($Text[$Index.Value] -eq '+' -or $Text[$Index.Value] -eq '-')) {
            $Index.Value++
        }
        if ($Index.Value -ge $Text.Length -or -not (Test-JsonDigit $Text[$Index.Value])) {
            throw "invalid JSON exponent at offset $($Index.Value)"
        }
        while ($Index.Value -lt $Text.Length -and (Test-JsonDigit $Text[$Index.Value])) {
            $Index.Value++
        }
    }
}

function Read-JsonValue {
    param([string]$Text, [ref]$Index)
    Skip-JsonWhitespace $Text $Index
    if ($Index.Value -ge $Text.Length) { throw 'expected JSON value' }

    $character = $Text[$Index.Value]
    if ($character -eq '"') {
        [void](Read-JsonString $Text $Index)
        return
    }
    if ($character -eq '{') {
        $Index.Value++
        Skip-JsonWhitespace $Text $Index
        $properties = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        if ($Index.Value -lt $Text.Length -and $Text[$Index.Value] -eq '}') {
            $Index.Value++
            return
        }
        while ($true) {
            Skip-JsonWhitespace $Text $Index
            $property = Read-JsonString $Text $Index
            if (-not $properties.Add($property)) {
                throw "duplicate JSON property '$property'"
            }
            Skip-JsonWhitespace $Text $Index
            if ($Index.Value -ge $Text.Length -or $Text[$Index.Value] -ne ':') {
                throw "expected ':' after JSON property at offset $($Index.Value)"
            }
            $Index.Value++
            Read-JsonValue $Text $Index
            Skip-JsonWhitespace $Text $Index
            if ($Index.Value -ge $Text.Length) { throw 'unterminated JSON object' }
            if ($Text[$Index.Value] -eq '}') {
                $Index.Value++
                return
            }
            if ($Text[$Index.Value] -ne ',') {
                throw "expected ',' in JSON object at offset $($Index.Value)"
            }
            $Index.Value++
        }
    }
    if ($character -eq '[') {
        $Index.Value++
        Skip-JsonWhitespace $Text $Index
        if ($Index.Value -lt $Text.Length -and $Text[$Index.Value] -eq ']') {
            $Index.Value++
            return
        }
        while ($true) {
            Read-JsonValue $Text $Index
            Skip-JsonWhitespace $Text $Index
            if ($Index.Value -ge $Text.Length) { throw 'unterminated JSON array' }
            if ($Text[$Index.Value] -eq ']') {
                $Index.Value++
                return
            }
            if ($Text[$Index.Value] -ne ',') {
                throw "expected ',' in JSON array at offset $($Index.Value)"
            }
            $Index.Value++
        }
    }
    if ($character -eq '-' -or (Test-JsonDigit $character)) {
        Read-JsonNumber $Text $Index
        return
    }
    foreach ($literal in @('true', 'false', 'null')) {
        if ($Index.Value + $literal.Length -le $Text.Length -and
            $Text.Substring($Index.Value, $literal.Length) -ceq $literal) {
            $Index.Value += $literal.Length
            return
        }
    }
    throw "invalid JSON value at offset $($Index.Value)"
}

function Assert-JsonHasUniqueProperties {
    param([string]$Text)
    $index = 0
    Read-JsonValue $Text ([ref]$index)
    Skip-JsonWhitespace $Text ([ref]$index)
    if ($index -ne $Text.Length) {
        throw "trailing content after JSON value at offset $index"
    }
}

function Compare-Ordinal {
    param([string]$Left, [string]$Right)
    return [string]::Compare($Left, $Right, [StringComparison]::Ordinal)
}

function Compare-EntryKey {
    param([object]$Left, [object]$Right)

    $comparison = Compare-Ordinal ([string]$Left.source) ([string]$Right.source)
    if ($comparison -ne 0) { return $comparison }

    $leftDefines = @($Left.defines)
    $rightDefines = @($Right.defines)
    $shared = [Math]::Min($leftDefines.Count, $rightDefines.Count)
    for ($i = 0; $i -lt $shared; $i++) {
        $comparison = Compare-Ordinal ([string]$leftDefines[$i]) ([string]$rightDefines[$i])
        if ($comparison -ne 0) { return $comparison }
    }
    if ($leftDefines.Count -ne $rightDefines.Count) {
        return $leftDefines.Count.CompareTo($rightDefines.Count)
    }
    return Compare-Ordinal ([string]$Left.profile) ([string]$Right.profile)
}

function Get-CanonicalKey {
    param([string]$Source, [object[]]$Defines, [string]$Profile)
    $separator = [char]31
    return $Source + $separator + (@($Defines) -join ([char]30)) + $separator + $Profile
}

function Test-SafeSourcePath {
    param(
        [object]$Source,
        [string]$Context,
        [System.Collections.Generic.List[string]]$Errors
    )

    if ($Source -isnot [string] -or [string]::IsNullOrWhiteSpace($Source)) {
        Add-ValidationError $Errors "$Context source must be a nonempty string"
        return $false
    }
    if ($Source.Contains('\') -or $Source.StartsWith('/') -or
        $Source -cnotmatch '^shaders/lighting/[A-Za-z0-9._-]+(?:/[A-Za-z0-9._-]+)*$') {
        Add-ValidationError $Errors "$Context source must be a normalized safe path under shaders/lighting"
        return $false
    }
    foreach ($segment in $Source.Split('/')) {
        if ($segment -eq '.' -or $segment -eq '..' -or $segment.Length -eq 0) {
            Add-ValidationError $Errors "$Context source contains an unsafe path segment"
            return $false
        }
    }
    return $true
}

if (-not (Test-Path -LiteralPath $RepoRoot -PathType Container)) {
    Exit-WithError "repository root not found: $RepoRoot" 2
}
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    Exit-WithError "manifest not found: $ManifestPath" 2
}

try {
    $manifestJson = Get-Content -LiteralPath $ManifestPath -Raw
    Assert-JsonHasUniqueProperties $manifestJson
    $manifest = $manifestJson | ConvertFrom-Json
} catch {
    Exit-WithError "malformed manifest JSON: $($_.Exception.Message)" 1
}

$errors = New-Object 'System.Collections.Generic.List[string]'
$topFields = @('schema', 'schema_version', 'evidence', 'compiler', 'entries')
$evidenceFields = @(
    'run_manifest_sha256',
    'contracts_sha256',
    'native_targets_sha256',
    'archive_sha256',
    'archive_member_sha256',
    'harness_version'
)
$compilerFields = @('name', 'entry_point', 'flags', 'include_root')
$entryFields = @('target', 'source', 'defines', 'profile', 'source_sha256', 'expected_dxbc_sha1')

Test-ExactFields $manifest $topFields 'manifest' $errors | Out-Null
if ($manifest.schema -isnot [string] -or $manifest.schema -cne 'fo4re.shader-fidelity-conformance') {
    Add-ValidationError $errors 'schema must be exactly fo4re.shader-fidelity-conformance'
}
$schemaVersionIsInteger = Test-Integer $manifest.schema_version
$harnessVersionIsInteger = Test-Integer $manifest.evidence.harness_version
if ($PSVersionTable.PSEdition -eq 'Desktop') {
    Add-Type -AssemblyName System.Web.Extensions
    $jsonReader = New-Object System.Web.Script.Serialization.JavaScriptSerializer
    $rawManifest = $jsonReader.DeserializeObject($manifestJson)
    if ($rawManifest -is [System.Collections.IDictionary]) {
        $schemaVersionIsInteger = Test-Integer $rawManifest['schema_version']
    } else {
        $schemaVersionIsInteger = $false
    }
    if ($rawManifest -is [System.Collections.IDictionary] -and
        $rawManifest['evidence'] -is [System.Collections.IDictionary]) {
        $harnessVersionIsInteger = Test-Integer $rawManifest['evidence']['harness_version']
    } else {
        $harnessVersionIsInteger = $false
    }
}
if (-not $schemaVersionIsInteger -or $manifest.schema_version -ne 1) {
    Add-ValidationError $errors 'schema_version must be integer 1'
}

Test-ExactFields $manifest.evidence $evidenceFields 'evidence' $errors | Out-Null
$hashFields = @(
    'run_manifest_sha256',
    'contracts_sha256',
    'native_targets_sha256',
    'archive_sha256',
    'archive_member_sha256'
)
foreach ($field in $hashFields) {
    $value = $manifest.evidence.$field
    if ($value -isnot [string] -or $value -cnotmatch '^[0-9a-f]{64}$') {
        Add-ValidationError $errors "evidence.$field must be lowercase 64-hex"
    }
}
if ($manifest.evidence.archive_sha256 -cne
    '4ac98b8fe72385f0cd13053bf5e81203fdaabe101377011e152457b705060659') {
    Add-ValidationError $errors 'evidence.archive_sha256 does not match the required archive'
}
if ($manifest.evidence.archive_member_sha256 -cne
    'f3254023504c4bab250162284a28efe2ed79fbf7a9b81e26d0fa9f22660d1d1f') {
    Add-ValidationError $errors 'evidence.archive_member_sha256 does not match the required archive member'
}
if (-not $harnessVersionIsInteger -or $manifest.evidence.harness_version -ne 4) {
    Add-ValidationError $errors 'evidence.harness_version must be integer 4'
}

Test-ExactFields $manifest.compiler $compilerFields 'compiler' $errors | Out-Null
if ($manifest.compiler.name -isnot [string] -or $manifest.compiler.name -cne 'fxc') {
    Add-ValidationError $errors 'compiler.name must be exactly fxc'
}
if ($manifest.compiler.entry_point -isnot [string] -or $manifest.compiler.entry_point -cne 'main') {
    Add-ValidationError $errors 'compiler.entry_point must be exactly main'
}
if ($manifest.compiler.include_root -isnot [string] -or
    $manifest.compiler.include_root -cne 'source-directory') {
    Add-ValidationError $errors 'compiler.include_root must be exactly source-directory'
}
if ($manifest.compiler.flags -isnot [System.Array]) {
    Add-ValidationError $errors 'compiler.flags must be an array'
} else {
    $flags = @($manifest.compiler.flags)
    foreach ($flag in $flags) {
        if ($flag -is [string] -and
            ($flag -match '^(?:/|-)(?:Fo|T|E|D|I)(?:$|.)' -or
             $flag -match '^--(?:output|profile|target|entry|entry-point|define|include|include-directory|source)(?:=|$)')) {
            Add-ValidationError $errors "compiler.flags must not bind output, profile, entry, define, include, or source: $flag"
        }
    }
    if ($flags.Count -ne 2 -or $flags[0] -isnot [string] -or
        $flags[1] -isnot [string] -or $flags[0] -cne '/nologo' -or $flags[1] -cne '/O3') {
        Add-ValidationError $errors 'compiler.flags must be exactly ["/nologo","/O3"]'
    }
}

if ($manifest.entries -isnot [System.Array]) {
    Add-ValidationError $errors 'entries must be an array'
    $entries = @()
} else {
    $entries = @($manifest.entries)
}

# Deliberate independent coverage friction: do not derive this set from the manifest.
$requiredEntries = @(
    [pscustomobject]@{ target = 'ambient_ibl_pass'; source = 'shaders/lighting/ambient_ibl_pass.hlsl'; defines = @(); profile = 'ps_5_0' },
    [pscustomobject]@{ target = 'ambient_ibl_pass_runtime'; source = 'shaders/lighting/ambient_ibl_pass_runtime.hlsl'; defines = @('TILELIGHT=1'); profile = 'ps_5_0' },
    [pscustomobject]@{ target = 'ambient_ibl_pass_runtime_no_tilelight'; source = 'shaders/lighting/ambient_ibl_pass_runtime.hlsl'; defines = @(); profile = 'ps_5_0' },
    [pscustomobject]@{ target = 'bsdf_light_deferred_directional'; source = 'shaders/lighting/bsdf_light_deferred.hlsl'; defines = @('LIGHT_TYPE=1'); profile = 'ps_5_0' },
    [pscustomobject]@{ target = 'bsdf_light_deferred_directional_ibl'; source = 'shaders/lighting/bsdf_light_deferred.hlsl'; defines = @('AMBIENT_IBL_IN_LIGHT=1', 'LIGHT_TYPE=1'); profile = 'ps_5_0' },
    [pscustomobject]@{ target = 'bsdf_light_deferred_point'; source = 'shaders/lighting/bsdf_light_deferred.hlsl'; defines = @('LIGHT_TYPE=2'); profile = 'ps_5_0' },
    [pscustomobject]@{ target = 'deferred_prepass'; source = 'shaders/lighting/deferred_prepass.hlsl'; defines = @(); profile = 'ps_5_0' },
    [pscustomobject]@{ target = 'vls_slice_scatter'; source = 'shaders/lighting/vls_slice_scatter.hlsl'; defines = @(); profile = 'ps_5_0' }
)

$targets = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
$keys = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
$manifestByTarget = New-Object 'System.Collections.Generic.Dictionary[string,object]' ([StringComparer]::Ordinal)
$manifestByKey = New-Object 'System.Collections.Generic.Dictionary[string,object]' ([StringComparer]::Ordinal)

for ($entryIndex = 0; $entryIndex -lt $entries.Count; $entryIndex++) {
    $entry = $entries[$entryIndex]
    $context = "entries[$entryIndex]"
    Test-ExactFields $entry $entryFields $context $errors | Out-Null

    if ($entry.target -isnot [string] -or [string]::IsNullOrWhiteSpace($entry.target)) {
        Add-ValidationError $errors "$context target must be a nonempty string"
    } elseif (-not $targets.Add($entry.target)) {
        Add-ValidationError $errors "$context duplicates target $($entry.target)"
    } else {
        $manifestByTarget.Add($entry.target, $entry)
    }

    $sourceIsSafe = Test-SafeSourcePath $entry.source $context $errors
    if ($entry.defines -isnot [System.Array]) {
        Add-ValidationError $errors "$context defines must be an array"
        $defines = @()
    } else {
        $defines = @($entry.defines)
        $defineSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        for ($defineIndex = 0; $defineIndex -lt $defines.Count; $defineIndex++) {
            $define = $defines[$defineIndex]
            if ($define -isnot [string] -or $define -cnotmatch '^[A-Za-z_][A-Za-z0-9_]*=.+$') {
                Add-ValidationError $errors "$context defines[$defineIndex] must be a NAME=VALUE string"
            } elseif (-not $defineSet.Add($define)) {
                Add-ValidationError $errors "$context has duplicate define $define"
            }
            if ($defineIndex -gt 0 -and
                (Compare-Ordinal ([string]$defines[$defineIndex - 1]) ([string]$define)) -ge 0) {
                Add-ValidationError $errors "$context defines must be sorted and unique"
            }
        }
    }

    if ($entry.profile -isnot [string] -or $entry.profile -cne 'ps_5_0') {
        Add-ValidationError $errors "$context profile must be exactly ps_5_0"
    }
    if ($entry.source_sha256 -isnot [string] -or
        $entry.source_sha256 -cnotmatch '^[0-9a-f]{64}$') {
        Add-ValidationError $errors "$context source_sha256 must be lowercase 64-hex"
    }
    if ($entry.expected_dxbc_sha1 -isnot [string] -or
        $entry.expected_dxbc_sha1 -cnotmatch '^[0-9a-f]{40}$') {
        Add-ValidationError $errors "$context expected_dxbc_sha1 must be lowercase 40-hex"
    }

    if ($sourceIsSafe -and $entry.profile -is [string]) {
        $key = Get-CanonicalKey $entry.source $defines $entry.profile
        if (-not $keys.Add($key)) {
            Add-ValidationError $errors "$context duplicates canonical key"
        } else {
            $manifestByKey.Add($key, $entry)
        }
    }
    if ($entryIndex -gt 0 -and (Compare-EntryKey $entries[$entryIndex - 1] $entry) -ge 0) {
        Add-ValidationError $errors 'entries must be sorted ascending by canonical key'
    }
}

foreach ($required in $requiredEntries) {
    if (-not $manifestByTarget.ContainsKey($required.target)) {
        Add-ValidationError $errors "missing required target $($required.target)"
    } else {
        $actual = $manifestByTarget[$required.target]
        $actualKey = Get-CanonicalKey $actual.source @($actual.defines) $actual.profile
        $requiredKey = Get-CanonicalKey $required.source @($required.defines) $required.profile
        if ($actualKey -cne $requiredKey) {
            Add-ValidationError $errors "target $($required.target) has the wrong canonical key"
        }
    }

    $requiredKey = Get-CanonicalKey $required.source @($required.defines) $required.profile
    if (-not $manifestByKey.ContainsKey($requiredKey)) {
        Add-ValidationError $errors "missing required canonical key for $($required.target)"
    } elseif ($manifestByKey[$requiredKey].target -cne $required.target) {
        Add-ValidationError $errors "canonical key for $($required.target) has the wrong target"
    }
}
foreach ($entry in $entries) {
    $requiredTarget = $requiredEntries | Where-Object { $_.target -ceq $entry.target } | Select-Object -First 1
    if ($null -eq $requiredTarget) {
        Add-ValidationError $errors "unexpected target $($entry.target)"
    }
    $requiredKeyMatch = $requiredEntries | Where-Object {
        (Get-CanonicalKey $_.source @($_.defines) $_.profile) -ceq
        (Get-CanonicalKey $entry.source @($entry.defines) $entry.profile)
    } | Select-Object -First 1
    if ($null -eq $requiredKeyMatch) {
        Add-ValidationError $errors "unexpected canonical key for target $($entry.target)"
    }
}
if ($entries.Count -ne 8) {
    Add-ValidationError $errors "entries must contain exactly 8 items; found $($entries.Count)"
}

$repoFullPath = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
foreach ($entry in $entries) {
    if (-not (Test-SafeSourcePath $entry.source "target $($entry.target)" $errors)) { continue }
    $relativePath = $entry.source.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $sourcePath = [IO.Path]::GetFullPath((Join-Path $RepoRoot $relativePath))
    if (-not $sourcePath.StartsWith($repoFullPath, [StringComparison]::OrdinalIgnoreCase)) {
        Add-ValidationError $errors "target $($entry.target) source escapes the repository"
        continue
    }
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        Add-ValidationError $errors "target $($entry.target) source not found: $($entry.source)"
        continue
    }
    try {
        $sourceHash = Get-LowerFileHash $sourcePath 'SHA256'
        if ($entry.source_sha256 -is [string] -and $sourceHash -cne $entry.source_sha256) {
            Add-ValidationError $errors "target $($entry.target) source_sha256 does not match checked-in source"
        }
    } catch {
        Add-ValidationError $errors "target $($entry.target) source hash failed: $($_.Exception.Message)"
    }
}

if ($errors.Count -gt 0) {
    foreach ($validationError in $errors) {
        [Console]::Error.WriteLine("ERROR: $validationError")
    }
    [Console]::Error.WriteLine("FAIL: manifest validation failed with $($errors.Count) error(s).")
    exit 1
}

$fxcCommand = $null
if (Test-Path -LiteralPath $FxcPath -PathType Leaf) {
    $fxcCommand = (Resolve-Path -LiteralPath $FxcPath).Path
} else {
    $command = Get-Command $FxcPath -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { $fxcCommand = $command.Source }
}
if (-not $fxcCommand) {
    Exit-WithError "fxc not found: $FxcPath" 2
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('shader-roundtrip-' + [Guid]::NewGuid().ToString('N'))
$results = @()
try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    foreach ($entry in $entries) {
        $sourcePath = [IO.Path]::GetFullPath(
            (Join-Path $RepoRoot ($entry.source.Replace('/', [IO.Path]::DirectorySeparatorChar))))
        $includePath = Split-Path -Parent $sourcePath
        $outputPath = Join-Path $tempRoot ($entry.target + '.dxbc')
        $arguments = @('/T', $entry.profile, '/E', 'main', '/nologo', '/O3', '/I', $includePath)
        foreach ($define in @($entry.defines)) {
            $arguments += @('/D', $define)
        }
        $arguments += @('/Fo', $outputPath, $sourcePath)

        $compilerOutput = $null
        $compileExit = 1
        try {
            $previousPreference = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $compilerOutput = & $fxcCommand @arguments 2>&1
                $compileExit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousPreference
            }
        } catch {
            $compilerOutput = $_.Exception.Message
        }

        if ($compileExit -ne 0) {
            $results += [pscustomobject]@{
                target = $entry.target
                expected = $entry.expected_dxbc_sha1
                actual = '<compile failed>'
                detail = ($compilerOutput | Out-String).Trim()
            }
            continue
        }
        if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
            $results += [pscustomobject]@{
                target = $entry.target
                expected = $entry.expected_dxbc_sha1
                actual = '<output missing>'
                detail = 'fxc succeeded without producing output'
            }
            continue
        }

        try {
            $actual = Get-LowerFileHash $outputPath 'SHA1'
            $detail = if ($actual -ceq $entry.expected_dxbc_sha1) { $null } else { 'DXBC SHA1 mismatch' }
        } catch {
            $actual = '<hash failed>'
            $detail = $_.Exception.Message
        }
        $results += [pscustomobject]@{
            target = $entry.target
            expected = $entry.expected_dxbc_sha1
            actual = $actual
            detail = $detail
        }
    }
} finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$failures = @($results | Where-Object { $_.actual -cne $_.expected })
if ($failures.Count -eq 0) {
    Write-Host "PASS: $($results.Count) / $($results.Count) shader round-trips matched."
    exit 0
}

foreach ($failure in $failures) {
    [Console]::Error.WriteLine(
        "ERROR: $($failure.target): expected $($failure.expected), actual $($failure.actual)")
    if ($failure.detail) {
        [Console]::Error.WriteLine("  $($failure.detail)")
    }
}
[Console]::Error.WriteLine("FAIL: $($failures.Count) / $($results.Count) shader round-trips failed.")
exit 1
