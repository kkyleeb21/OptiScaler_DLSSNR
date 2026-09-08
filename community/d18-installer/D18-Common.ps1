Set-StrictMode -Version 2.0

function Set-D18IniValue {
    param([string]$Text, [string]$Section, [string]$Key, [string]$Value)
    $sectionPattern = '(?ims)^\[' + [regex]::Escape($Section) + '\][^\r\n]*\r?\n(?<body>.*?)(?=^\[|\z)'
    $match = [regex]::Match($Text, $sectionPattern)
    if (-not $match.Success) { return $Text.TrimEnd() + "`r`n[$Section]`r`n$Key=$Value`r`n" }
    $block = $match.Value
    $keyPattern = '(?im)^' + [regex]::Escape($Key) + '\s*=[^\r\n]*'
    if ([regex]::IsMatch($block, $keyPattern)) {
        $block = [regex]::Replace($block, $keyPattern, "$Key=$Value")
    } else { $block = $block.TrimEnd() + "`r`n$Key=$Value`r`n`r`n" }
    return $Text.Substring(0, $match.Index) + $block + $Text.Substring($match.Index + $match.Length)
}

function Get-D18ProxyRecommendation {
    param([string]$Game, [bool]$IsRE = $false)
    if (Test-Path -LiteralPath (Join-Path $Game 'Endfield.exe')) {
        return [pscustomobject]@{Name='d3d12.dll';Reason='Endfield: keep the verified d3d12.dll loader name.'}
    }
    if ($IsRE) { return [pscustomobject]@{Name='d3d12.dll';Reason='RE Engine integration uses d3d12.dll.'} }
    return [pscustomobject]@{Name='dxgi.dll';Reason='Default loader name; alternate names are available for plugin coexistence.'}
}

function Select-D18ProxyName {
    param([string]$Requested, [string]$Previous, [string]$Recommended, [switch]$AssumeYes)
    $names=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll')
    if ($Requested) { if ($Requested -notin $names) { throw 'Unsupported proxy name' }; return $Requested }
    if ($Previous -in $names) { return $Previous }
    if ($AssumeYes) { return $Recommended }
    Write-Host 'Proxy DLL name (Enter accepts recommendation):'
    for($i=0;$i -lt $names.Count;$i++){Write-Host ('  {0}. {1}{2}' -f ($i+1),$names[$i],$(if($names[$i] -eq $Recommended){' (recommended)'}else{''}))}
    while($true){
        $answer=Read-Host 'Choose 1-5'
        if(-not $answer){return $Recommended}
        if($answer -match '^[1-5]$'){return $names[[int]$answer-1]}
        if($answer -in $names){return $answer}
        Write-Host 'Enter 1-5 or a supported DLL name.'
    }
}

function Get-D18Sha256 {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    return (Get-FileHash -Algorithm SHA256 -LiteralPath $LiteralPath).Hash.ToUpperInvariant()
}

function Test-D18BytesAtOffset {
    param(
        # Do not type this parameter as [byte[]]. Windows PowerShell 5.1 copies/coerces the
        # complete array every time an advanced function binds a typed array parameter. The
        # DLSSNR Runtime is about 165 MB, turning each tiny layout guard into a multi-second
        # operation. Accept the existing array by reference and validate its type explicitly.
        [Parameter(Mandatory = $true)]$Buffer,
        [Parameter(Mandatory = $true)][long]$Offset,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][byte[]]$Expected
    )

    if ($Buffer -isnot [byte[]]) {
        throw 'D18 byte guard requires a byte-array buffer.'
    }

    if ($Offset -lt 0 -or $Offset + $Expected.Length -gt $Buffer.LongLength) {
        return $false
    }

    for ($i = 0; $i -lt $Expected.Length; $i++) {
        if ($Buffer[$Offset + $i] -ne $Expected[$i]) {
            return $false
        }
    }
    return $true
}

function New-D18PatchedRuntime {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [string]$PatchManifest = (Join-Path $PSScriptRoot 'runtime_patch.json')
    )

    $source = (Resolve-Path -LiteralPath $SourcePath).Path
    $outputFull = [System.IO.Path]::GetFullPath($OutputPath)
    if ([string]::Equals($source, $outputFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to patch the source Runtime in place.'
    }

    $patch = Get-Content -Raw -LiteralPath $PatchManifest | ConvertFrom-Json
    if ($patch.format -ne 'dlssnr-d18-guarded-layout-patch-v2') {
        throw "Unsupported Runtime patch format: $($patch.format)"
    }
    if (@($patch.hunks).Count -eq 0) {
        throw 'Runtime patch manifest contains no guards.'
    }

    $sourceHash = Get-D18Sha256 -LiteralPath $source
    $inputBytes = [System.IO.File]::ReadAllBytes($source)

    $requiredLength = [long]$inputBytes.LongLength
    foreach ($hunk in $patch.hunks) {
        $replacementLength = if ($hunk.PSObject.Properties.Name -contains 'replacement_base64_parts') {
            [System.Convert]::FromBase64String((-join @($hunk.replacement_base64_parts))).Length
        }
        else {
            [System.Convert]::FromBase64String([string]$hunk.replacement_base64).Length
        }
        $requiredLength = [Math]::Max($requiredLength, [long]$hunk.offset + $replacementLength)
    }

    if ($requiredLength -gt [int]::MaxValue) {
        throw "Runtime patch output is too large: $requiredLength bytes."
    }

    $outputBytes = New-Object byte[] ([int]$requiredLength)
    [System.Array]::Copy($inputBytes, 0, $outputBytes, 0, $inputBytes.Length)

    $appliedHunks = 0
    $compatibleVariantHunks = 0
    $alreadyPatchedHunks = 0

    foreach ($hunk in $patch.hunks) {
        $offset = [long]$hunk.offset
        $expected = [System.Convert]::FromBase64String([string]$hunk.expected_base64)
        if ($hunk.PSObject.Properties.Name -contains 'replacement_base64_parts') {
            $replacementText = -join @($hunk.replacement_base64_parts)
        }
        else {
            $replacementText = [string]$hunk.replacement_base64
        }
        $replacement = [System.Convert]::FromBase64String($replacementText)

        $replacementPresent = Test-D18BytesAtOffset -Buffer $inputBytes -Offset $offset -Expected $replacement
        if ($replacementPresent) {
            $alreadyPatchedHunks++
            continue
        }

        # An empty expected sequence is the guarded append point. It is valid only when the input
        # ends exactly there. A longer unknown file must already carry the complete replacement;
        # otherwise writing at this offset could overwrite another mod's appended data.
        $expectedPresent = if ($expected.Length -eq 0) {
            $inputBytes.LongLength -eq $offset
        }
        else {
            Test-D18BytesAtOffset -Buffer $inputBytes -Offset $offset -Expected $expected
        }
        $compatibleVariantPresent = $false
        if (-not $expectedPresent -and
            $hunk.PSObject.Properties.Name -contains 'compatible_input_base64') {
            foreach ($variantText in @($hunk.compatible_input_base64)) {
                $variant = [System.Convert]::FromBase64String([string]$variantText)
                if (Test-D18BytesAtOffset -Buffer $inputBytes -Offset $offset -Expected $variant) {
                    $compatibleVariantPresent = $true
                    break
                }
            }
        }
        if (-not $expectedPresent -and -not $compatibleVariantPresent) {
            throw (('[CONFLICT] This Runtime conflicts with the installer at offset 0x{0:X}. ' +
                    'D18 cannot safely apply its patch. The source file was NOT modified. ' +
                    'Please use a verified Runtime listed in README_CN.md / README.md. Input SHA256: {1}') -f $offset, $sourceHash)
        }
        if ($offset + $replacement.Length -gt $outputBytes.LongLength) {
            throw ('Runtime replacement exceeds output at file offset 0x{0:X}.' -f $offset)
        }
        [System.Array]::Copy($replacement, 0, $outputBytes, $offset, $replacement.Length)
        $appliedHunks++
        if ($compatibleVariantPresent) {
            $compatibleVariantHunks++
        }
    }

    foreach ($hunk in $patch.hunks) {
        $offset = [long]$hunk.offset
        $replacementText = if ($hunk.PSObject.Properties.Name -contains 'replacement_base64_parts') {
            -join @($hunk.replacement_base64_parts)
        }
        else {
            [string]$hunk.replacement_base64
        }
        $replacement = [System.Convert]::FromBase64String($replacementText)
        if (-not (Test-D18BytesAtOffset -Buffer $outputBytes -Offset $offset -Expected $replacement)) {
            throw ('Patched Runtime verification failed at file offset 0x{0:X}.' -f $offset)
        }
    }

    $outputDirectory = Split-Path -Parent $outputFull
    if (-not (Test-Path -LiteralPath $outputDirectory)) {
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    }

    $temporary = "$outputFull.tmp-$([guid]::NewGuid().ToString('N'))"
    try {
        [System.IO.File]::WriteAllBytes($temporary, $outputBytes)
        $outputHash = Get-D18Sha256 -LiteralPath $temporary
        Move-Item -LiteralPath $temporary -Destination $outputFull -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }

    # Hash recognition is informational, never a substitute for every layout guard above.
    # An unknown file with all replacement bytes is already patched, NOT a verified release.
    $recognized = $sourceHash -eq [string]$patch.reference_source_sha256 -or
                  $sourceHash -eq [string]$patch.reference_output_sha256
    $classification = if ($recognized) { 'VERIFIED' }
                      elseif ($appliedHunks -eq 0) { 'ALREADY_PATCHED' }
                      else { 'UNVERIFIED_COMPATIBLE' }
    return [pscustomobject]@{
        Classification = $classification
        RecognizedReference = $recognized
        Path = $outputFull
        SourceSha256 = $sourceHash
        OutputSha256 = $outputHash
        SourceSize = [long]$inputBytes.LongLength
        OutputSize = [long]$outputBytes.LongLength
        AppliedHunks = $appliedHunks
        CompatibleVariantHunks = $compatibleVariantHunks
        AlreadyPatchedHunks = $alreadyPatchedHunks
    }
}

function Test-D18Payload {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$PayloadRoot,
        [Parameter(Mandatory = $true)][string]$ManifestPath
    )

    $manifest = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json
    foreach ($entry in $manifest.files) {
        $path = Join-Path $PayloadRoot ([string]$entry.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Release payload is incomplete: $($entry.path) is missing."
        }
        $actual = Get-D18Sha256 -LiteralPath $path
        if ($actual -ne [string]$entry.sha256) {
            throw "Release payload hash mismatch for $($entry.path): expected $($entry.sha256), got $actual."
        }
    }
    return $manifest
}

function Get-D18TargetRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$PayloadRelativePath,
        [Parameter(Mandatory = $true)][string]$ProxyName
    )

    if ($PayloadRelativePath -eq 'OptiScaler.dll') {
        return $ProxyName
    }
    if ($PayloadRelativePath -eq 'OptiScaler.ini.d18') {
        return 'OptiScaler.ini'
    }
    return $PayloadRelativePath
}

function ConvertTo-D18UiKey {
    param([string]$Name)
    $nameUpper = $Name.Trim().ToUpperInvariant()
    if (-not $nameUpper) { $nameUpper = 'INSERT' }
    $keys = @{ INSERT=45; HOME=36; END=35; PGUP=33; PGDN=34; DELETE=46;
               TAB=9; SPACE=32; PAUSE=19; SCROLLLOCK=145; BACKSPACE=8 }
    if ($keys.ContainsKey($nameUpper)) { return [int]$keys[$nameUpper] }
    if ($nameUpper -match '^F([1-9]|1[0-9]|2[0-4])$') { return 111 + [int]$Matches[1] }
    if ($nameUpper -match '^[A-Z0-9]$') { return [int][char]$nameUpper }
    throw 'Use Insert, Home, End, PgUp, PgDn, Delete, Tab, Space, Pause, ScrollLock, Backspace, F1-F24, A-Z or 0-9 (single key, no modifiers).'
}

function Get-D18UiKey {
    param([string]$Text)
    $sections = [regex]::Matches($Text, '(?ims)^[ \t]*\[Menu\][^\r\n]*(?:\r?\n|\z).*?(?=^[ \t]*\[|\z)')
    if ($sections.Count -gt 1) { throw 'Duplicate Menu sections; resolve before installation.' }
    if (-not $sections.Count) { return 'auto' }
    $entries = [regex]::Matches($sections[0].Value, '(?im)^[ \t]*ShortcutKey[ \t]*=([^\r\n]*)')
    if ($entries.Count -gt 1) { throw 'Duplicate Menu ShortcutKey; resolve before installation.' }
    if ($entries.Count) { return $entries[0].Groups[1].Value.Trim() }
    return 'auto'
}

function Set-D18UiKey {
    param([string]$Text, [string]$Value)
    if ($Value -match '[\r\n]') { throw 'Invalid multiline UI key' }
    $null = Get-D18UiKey -Text $Text # Reject ambiguous sections/keys before writing.
    $section = [regex]::Match($Text, '(?ims)^[ \t]*\[Menu\][^\r\n]*(?:\r?\n|\z).*?(?=^[ \t]*\[|\z)')
    if (-not $section.Success) { return $Text.TrimEnd()+"`r`n`r`n[Menu]`r`nShortcutKey=$Value`r`n" }
    $block = $section.Value
    $entry = [regex]::Match($block, '(?im)^[ \t]*ShortcutKey[ \t]*=[^\r\n]*')
    if ($entry.Success) { $block = $block.Remove($entry.Index,$entry.Length).Insert($entry.Index,"ShortcutKey=$Value") }
    else { $block = $block.TrimEnd()+"`r`nShortcutKey=$Value`r`n" }
    return $Text.Remove($section.Index,$section.Length).Insert($section.Index,$block)
}

function Confirm-D18Choice {
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [switch]$AssumeYes
    )

    if ($AssumeYes) {
        return $true
    }
    $answer = Read-Host "$Prompt [y/N]"
    return $answer -match '^(y|yes)$'
}
