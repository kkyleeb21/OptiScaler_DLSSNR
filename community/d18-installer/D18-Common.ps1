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

function Set-D18FreshDefaults {
    # For a new installation or a distributable template only. Never reset an existing user's INI.
    param([string]$Text)
    $policy = [ordered]@{
        DlssNr = [ordered]@{ Enabled='false'; NativeSrEnabled='false'; HighResolution='false'; PassCount='1'; SharedHistory='false'; Compare='0'; DebugView='0'; Diagnostics='0'; AutoCapture='false'; ExperimentalCompose='false'; ProxyProbe='false'; Sh0Enabled='false'; Sh0HalfG2='false'; PreserveHighFrequency='false'; MotionAdaptive='false'; LinearResolve='false'; LinearColorInput='false'; CustomColorFilter='false'; CatmullRomInput='false'; GuidedReconstruction='false'; GainFirstReconstruction='false'; RelativeColour='false'; WhitePointFromExposure='true'; InternalScaling='true'; InternalScalingRatio='1.0'; AutoMask='true'; JitterCorrection='false'; CompareSwap='false'; CompareTags='false'; V8Mode='0'; HighlightEncoding='0' }
        DlssNrPass2 = @{ Scaling='false'; AutoMask='false' }
        DlssNrPass3 = @{ Scaling='false'; AutoMask='false' }
        DlssNrPass4 = @{ Scaling='false'; AutoMask='false' }
        FrameGen = [ordered]@{ Enabled='false'; FGInput='NoFG'; FGOutput='NoFG'; DebugView='false' }
        Sharpness = @{ OverrideSharpness='false' }
        CAS = @{ Enabled='false'; MotionSharpnessEnabled='false'; ContrastEnabled='false' }
        OutputScaling = @{ Enabled='false' }
        Magnifier = @{ Enabled='false' }
        Menu = @{ ShowFps='false' }
        DLSS = @{ RenderPresetOverride='false' }
        DLSSD = @{ RenderPresetOverride='false' }
        Plugins = @{ LoadReshade='false' }
        Log = @{ LogToFile='false'; LogToConsole='false' }
        UpscaleRatio = @{ UpscaleRatioOverrideEnabled='false' }
        QualityOverrides = @{ QualityRatioOverrideEnabled='false' }
    }
    foreach ($section in $policy.Keys) {
        foreach ($key in $policy[$section].Keys) {
            $Text = Set-D18IniValue -Text $Text -Section $section -Key $key -Value $policy[$section][$key]
        }
    }
    return $Text
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

function Test-D18AppendSection {
    param($Buffer, [long]$Offset, [long]$Length)
    # Byte parsing only. Require the complete appended RX section in the PE table
    # and inside SizeOfImage, including for legacy-v2 manifests.
    if ($Buffer.LongLength -lt 64) { return $false }
    $pe = [long][BitConverter]::ToUInt32($Buffer, 60)
    if ($pe -gt $Buffer.LongLength - 264) { return $false }
    $count = [BitConverter]::ToUInt16($Buffer, [int]($pe + 6))
    $imageSize = [long][BitConverter]::ToUInt32($Buffer, [int]($pe + 80))
    if ($count -lt 1 -or $count -gt 96 -or $pe + 264 + $count * 40 -gt $Buffer.LongLength) { return $false }
    for ($i=0; $i -lt $count; $i++) {
        $at = [int]($pe + 264 + $i * 40)
        $virtualSize = [long][BitConverter]::ToUInt32($Buffer, $at + 8)
        $rva = [long][BitConverter]::ToUInt32($Buffer, $at + 12)
        $rawSize = [long][BitConverter]::ToUInt32($Buffer, $at + 16)
        $raw = [long][BitConverter]::ToUInt32($Buffer, $at + 20)
        $flags = [long][BitConverter]::ToUInt32($Buffer, $at + 36)
        if ($raw -lt $Offset -or $raw + $rawSize -ne $Offset + $Length) { continue }
        if ($rawSize -gt 0 -and $virtualSize -gt 0 -and $rva + [Math]::Max($rawSize, $virtualSize) -le $imageSize -and
            $raw + $rawSize -le $Buffer.LongLength -and ($flags -band 3758096384L) -eq 1610612736L) { return $true }
    }
    return $false
}

function Get-D18PatchState {
    param($Buffer, $Patch)
    # Intersect whole-profile states. Equal expected/replacement bytes remain
    # neutral; compatible variants belong to the original state only.
    $states = 3
    foreach ($hunk in $Patch.hunks) {
        $replacementText = if ($hunk.PSObject.Properties.Name -contains 'replacement_base64_parts') { -join @($hunk.replacement_base64_parts) } else { [string]$hunk.replacement_base64 }
        $expected = [Convert]::FromBase64String([string]$hunk.expected_base64)
        $replacement = [Convert]::FromBase64String($replacementText)
        $matches = 0
        if (Test-D18BytesAtOffset -Buffer $Buffer -Offset $hunk.offset -Expected $replacement) {
            if ($expected.Length -ne 0 -or (Test-D18AppendSection -Buffer $Buffer -Offset $hunk.offset -Length $replacement.Length)) { $matches = 2 }
        }
        $original = if ($expected.Length -eq 0) { $Buffer.LongLength -eq [long]$hunk.offset }
                    else { Test-D18BytesAtOffset -Buffer $Buffer -Offset $hunk.offset -Expected $expected }
        if ($original) { $matches = $matches -bor 1 }
        if ($hunk.PSObject.Properties.Name -contains 'compatible_input_base64') {
            foreach ($variant in $hunk.compatible_input_base64) {
                if (Test-D18BytesAtOffset -Buffer $Buffer -Offset $hunk.offset -Expected ([Convert]::FromBase64String($variant))) { $matches = $matches -bor 1; break }
            }
        }
        $states = $states -band $matches
        if ($states -eq 0) { return 0 }
    }
    return $states
}

function Test-D18ProfileGuards {
    param($Buffer, $Profile)
    foreach ($guard in @($Profile.guards)) {
        $variants = @([string]$guard.expected_hex)
        if ($guard.PSObject.Properties.Name -contains 'compatible_input_hex') { $variants += @($guard.compatible_input_hex) }
        $match = $false
        foreach ($hex in $variants) {
            $hex = [regex]::Replace($hex, '\s', '')
            $expected = New-Object byte[] ($hex.Length / 2)
            for ($i=0; $i -lt $expected.Length; $i++) { $expected[$i] = [Convert]::ToByte($hex.Substring($i*2,2),16) }
            if (Test-D18BytesAtOffset -Buffer $Buffer -Offset $guard.offset -Expected $expected) { $match=$true; break }
        }
        if (-not $match) { return $false }
    }
    return $true
}

function Test-D18ProfileBytes {
    param($Buffer, $Profile)
    return (Test-D18ProfileGuards -Buffer $Buffer -Profile $Profile) -and
           (Get-D18PatchState -Buffer $Buffer -Patch $Profile) -ne 0
}

function Get-D18OptionalValue {
    param($Object, [string]$Name, $Default = '')
    if ($Object.PSObject.Properties.Name -contains $Name) { return $Object.$Name }
    return $Default
}

function Get-D18RuntimeVersions {
    param($Manifest)
    # Display names come from the registered numeric profile versions, in manifest order.
    $profiles = if ($Manifest.format -eq 'dlssnr-d18-runtime-profiles-v3') { @($Manifest.profiles) } else { @($Manifest) }
    @(foreach ($profile in $profiles) {
        if ($profile.PSObject.Properties.Name -contains 'version') {
            $parts = @($profile.version)
            while ($parts.Count -gt 2 -and $parts[-1] -eq 0) { $parts = $parts[0..($parts.Count-2)] }
            $parts -join '.'
        }
    }) | Select-Object -Unique
}

function New-D18RuntimeConflict {
    param([string]$Reason, $Version, $Manifest)
    $detected = if ([string]::IsNullOrWhiteSpace($Version.FileVersion)) { '' }
                else { @($Version.FileMajorPart,$Version.FileMinorPart,$Version.FileBuildPart,$Version.FilePrivatePart) -join '.' }
    $supported = @(Get-D18RuntimeVersions $Manifest)
    $list = $supported -join ', '
    $enVersion = if ($detected) { $detected } else { 'unknown (no version resource)' }
    $zhVersion = if ($detected) { $detected } else { '未知（无版本资源）' }
    $identity = "$($Version.FileDescription) $($Version.ProductName)"
    $en = switch ($Reason) {
        'NOT_NR_RUNTIME' {
            if ($identity -match '(?i)DLSS.?G|Frame.?Generation') { 'This is a DLSS frame generation file, not an NR file.' }
            elseif ($identity -match '(?i)DLSS.?D|Ray.?Reconstruction') { 'This is a DLSS ray reconstruction file, not an NR file.' }
            elseif ($identity -match '(?i)DLSS|Super.?Resolution') { 'This is a DLSS super resolution file, not an NR file.' }
            else { 'No NR version resource was found.' }
        }
        'UNSUPPORTED_VERSION' { 'This NR version is not supported.' }
        'PATCH_STATE_INCOMPLETE' { 'The patch state is incomplete or mixed. Use an unmodified NR file.' }
        default { 'No registered patch layout matches. The file may have been modified by another tool; use an unmodified NR file.' }
    }
    $zh = switch ($Reason) {
        'NOT_NR_RUNTIME' {
            if ($identity -match '(?i)DLSS.?G|Frame.?Generation') { '这是 DLSS 帧生成文件，不是 NR 文件。' }
            elseif ($identity -match '(?i)DLSS.?D|Ray.?Reconstruction') { '这是 DLSS 光线重建文件，不是 NR 文件。' }
            elseif ($identity -match '(?i)DLSS|Super.?Resolution') { '这是 DLSS 超分文件，不是 NR 文件。' }
            else { '未找到 NR 版本资源。' }
        }
        'UNSUPPORTED_VERSION' { '此 NR 版本不受支持。' }
        'PATCH_STATE_INCOMPLETE' { '修补状态不完整或混合，请换一份未修改的 NR 文件。' }
        default { '修补点字节与登记布局不匹配。文件可能被其他工具改过，请换一份未修改的 NR 文件。' }
    }
    $messageEn = "Detected version: $enVersion. $en"
    $messageZh = "检测到的版本：${zhVersion}。$zh"
    if ($list) {
        $messageEn += " Supported versions: $list."
        $messageZh += " 支持的版本：${list}。"
        if ($Reason -notin @('GUARD_BYTES_MISMATCH', 'PATCH_STATE_INCOMPLETE')) {
            $messageEn += ' Select an nvngx_dlssnr.dll with one of these versions.'
            $messageZh += '请换成这些版本的 nvngx_dlssnr.dll。'
        }
    }
    [pscustomobject]@{Classification='CONFLICT';ConflictReason=$Reason;DetectedVersion=$detected;
        SupportedVersions=$supported;MessageEn=$messageEn;MessageZh=$messageZh}
}

function Stop-D18RuntimeConflict {
    param($Conflict)
    $errorObject = [InvalidOperationException]::new("[CONFLICT] $($Conflict.MessageEn) / $($Conflict.MessageZh)")
    $errorObject.Data['D18RuntimeConflict'] = $Conflict
    throw $errorObject
}

function Get-D18RuntimeConflictReason {
    param($Buffer, $Profiles, $Version)
    # Diagnostic only: called after the existing profile/version/byte admission failed.
    $identity = "$($Version.FileDescription) $($Version.ProductName)"
    if ([string]::IsNullOrWhiteSpace($Version.FileVersion) -or
        ($identity -notmatch '(?i)DLSS.?NR' -and $identity -match '(?i)DLSS|Super.?Resolution|Ray.?Reconstruction|Frame.?Generation')) {
        return 'NOT_NR_RUNTIME'
    }
    if (@($Profiles).Count -eq 0) { return 'UNSUPPORTED_VERSION' }
    foreach ($profile in $Profiles) {
        if (-not (Test-D18ProfileGuards -Buffer $Buffer -Profile $profile)) { continue }
        foreach ($hunk in $profile.hunks) {
            $text = if ($hunk.PSObject.Properties.Name -contains 'replacement_base64_parts') { -join @($hunk.replacement_base64_parts) } else { [string]$hunk.replacement_base64 }
            $expected = [Convert]::FromBase64String([string]$hunk.expected_base64)
            $replacement = [Convert]::FromBase64String($text)
            # Only a replacement distinct from the original is evidence of partial patching.
            if ([Convert]::ToBase64String($expected) -ne [Convert]::ToBase64String($replacement) -and
                (Test-D18BytesAtOffset -Buffer $Buffer -Offset $hunk.offset -Expected $replacement)) {
                return 'PATCH_STATE_INCOMPLETE'
            }
        }
    }
    return 'GUARD_BYTES_MISMATCH'
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

    $patch = Get-Content -Raw -Encoding UTF8 -LiteralPath $PatchManifest | ConvertFrom-Json
    $sourceHash = Get-D18Sha256 -LiteralPath $source
    $inputBytes = [System.IO.File]::ReadAllBytes($source)
    $manifest = $patch
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($source)
    $profileId = 'legacy-v2'
    $experimental = $false
    if ($patch.format -eq 'dlssnr-d18-runtime-profiles-v3') {
        $parts = @($version.FileMajorPart,$version.FileMinorPart,$version.FileBuildPart,$version.FilePrivatePart)
        $versionProfiles = @($patch.profiles | Where-Object { ($_.version -join '.') -eq ($parts -join '.') })
        $matches = @($versionProfiles | Where-Object { Test-D18ProfileBytes -Buffer $inputBytes -Profile $_ })
        if ($matches.Count -ne 1) {
            $reason = Get-D18RuntimeConflictReason -Buffer $inputBytes -Profiles $versionProfiles -Version $version
            Stop-D18RuntimeConflict (New-D18RuntimeConflict $reason $version $manifest)
        }
        $patch = $matches[0]
        $profileId = [string]$patch.id
        $experimental = [bool]$patch.experimental
        if ($experimental) { Write-Warning "Runtime $profileId : experimental, unsigned, unknown provenance / 实验、未签名、来源不明" }
    }
    if ($patch.format -ne 'dlssnr-d18-guarded-layout-patch-v2') {
        throw "Unsupported Runtime patch format: $($patch.format)"
    }
    if (@($patch.hunks).Count -eq 0) {
        throw 'Runtime patch manifest contains no guards.'
    }
    if ((Get-D18PatchState -Buffer $inputBytes -Patch $patch) -eq 0) {
        Stop-D18RuntimeConflict (New-D18RuntimeConflict 'PATCH_STATE_INCOMPLETE' $version $manifest)
    }

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
    $recognized = $sourceHash -eq [string](Get-D18OptionalValue $patch 'reference_source_sha256') -or
                  $sourceHash -eq [string](Get-D18OptionalValue $patch 'reference_output_sha256')
    $classification = if ($experimental) { 'EXPERIMENTAL_UNSIGNED' }
                      elseif ($recognized) { 'VERIFIED' }
                      elseif ($appliedHunks -eq 0) { 'ALREADY_PATCHED' }
                      else { 'UNVERIFIED_COMPATIBLE' }
    return [pscustomobject]@{
        Profile = $profileId
        Experimental = $experimental
        Notice = [string](Get-D18OptionalValue $patch 'notice')
        ConflictReason = ''
        DetectedVersion = $(if ($version.FileVersion) { @($version.FileMajorPart,$version.FileMinorPart,$version.FileBuildPart,$version.FilePrivatePart) -join '.' } else { '' })
        SupportedVersions = @(Get-D18RuntimeVersions $manifest)
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

# The helper and native addon compile the same validator and generated rule table.
function Test-D18Dx11Runtime {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$RuntimePath,
          [Parameter(Mandatory=$true)][string]$CheckerPath)
    if (-not (Test-Path -LiteralPath $CheckerPath -PathType Leaf)) {
        throw 'DX11 Runtime checker is missing. Use the complete installer package.'
    }
    $text = & $CheckerPath $RuntimePath
    $code = $LASTEXITCODE
    try { $result = ($text -join "`n") | ConvertFrom-Json }
    catch { throw 'DX11 Runtime checker did not return a valid result.' }
    if ($null -eq $result -or -not ($result.PSObject.Properties.Name -contains 'accepted') -or
        -not ($result.PSObject.Properties.Name -contains 'rule')) { throw 'Incomplete DX11 Runtime check result.' }
    if ($code -ne 0 -or $result.accepted -ne $true) {
        throw ('[DX11_LAYOUT_CONFLICT] {0}: {1}, region {2}, file offset 0x{3:X}. Current installation has not been changed.' -f
            $result.rule,$result.reason,$result.region,[long]$result.offset)
    }
    return $result
}
function Test-D18PathRoot {
    # Pure string check: no drive enumeration, filesystem access or writes.
    param([string]$Path)
    $p = $Path.Replace('/', '\')
    $p = $p -replace '^\\\\\?\\UNC\\', '\\'
    $p = $p -replace '^\\\\\?\\', ''
    return ($p -match '^[A-Za-z]:\\*$' -or $p -match '^\\\\[^\\]+\\[^\\]+\\*$')
}

function Get-D18ProtectedDirectories {
    # Exact folder identities, including redirected Windows known folders.
    @($env:SystemRoot, $env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:USERPROFILE,
      [Environment]::GetFolderPath('UserProfile'), [Environment]::GetFolderPath('Desktop'),
      [Environment]::GetFolderPath('MyDocuments'), [Environment]::GetFolderPath('MyPictures'),
      [Environment]::GetFolderPath('MyMusic'), [Environment]::GetFolderPath('MyVideos')) | Where-Object { $_ }
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders'
    $folders = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
    $downloadsKnown = $false
    if ($folders) {
        foreach ($name in @('Desktop','Personal','{374DE290-123F-4565-9164-39C4925E467B}')) {
            $value = $folders.PSObject.Properties[$name]
            if ($value -and $value.Value) {
                [Environment]::ExpandEnvironmentVariables([string]$value.Value)
                if ($name -eq '{374DE290-123F-4565-9164-39C4925E467B}') { $downloadsKnown = $true }
            }
        }
    }
    if (-not $downloadsKnown -and $env:USERPROFILE) { Join-Path $env:USERPROFILE 'Downloads' }
}

function Get-D18GameDirectoryScope {
    param([string]$Path)
    $code = ''; $zh = ''; $en = ''; $full = $Path
    try {
        $normalized=$Path.Replace('/', '\') -replace '^\\\\\?\\UNC\\', '\\' -replace '^\\\\\?\\', ''
        $full = [IO.Path]::GetFullPath($normalized)
    } catch { $code = 'invalid_path' }
    if ($code -or [string]::IsNullOrWhiteSpace($Path)) {
        $code='invalid_path'; $zh='目标路径不存在或不是目录。'; $en='The target path does not exist or is not a directory.'
    } elseif (Test-D18PathRoot $full) {
        $code='path_root'; $zh='不能安装到盘符根目录或 UNC 共享根目录，请选择游戏 EXE 所在目录。'
        $en='Cannot install into a drive root or UNC share root. Choose the folder containing the game EXE.'
    } else {
        $full = $full.TrimEnd('\')
        foreach ($protected in @(Get-D18ProtectedDirectories)) {
            try { $protected = [IO.Path]::GetFullPath($protected).TrimEnd('\') } catch { continue }
            $ancestor = $protected.StartsWith($full+'\', [StringComparison]::OrdinalIgnoreCase)
            $windowsChild = $env:SystemRoot -and $full.StartsWith($env:SystemRoot.TrimEnd('\')+'\', [StringComparison]::OrdinalIgnoreCase)
            if ($full -ieq $protected -or $ancestor -or $windowsChild) {
                $code='protected_directory'; $zh='不能安装到 Windows 系统目录、Program Files 本身、用户主目录、桌面、下载、文档、图片、音乐、视频文件夹本身或这些目录的上层。请选择具体游戏目录。'
                $en='Cannot install into Windows system directories, Program Files itself, the user home, Desktop, Downloads, Documents, Pictures, Music, Videos themselves, or their parents. Choose a specific game folder.'
                break
            }
        }
        if (-not $code) {
            if (-not (Test-Path -LiteralPath $full -PathType Container)) {
                $code='invalid_path'; $zh='目标路径不存在或不是目录。'; $en='The target path does not exist or is not a directory.'
            } elseif (-not @(Get-ChildItem -LiteralPath $full -Filter '*.exe' -File -Force -ErrorAction Stop).Count) {
                $code='no_top_level_exe'; $zh='目标目录顶层没有 EXE 文件，请选择游戏 EXE 所在目录。'
                $en='The target folder has no top-level EXE file. Choose the folder containing the game EXE.'
            }
        }
    }
    return [pscustomobject]@{ allowed=(-not $code); code=$code; path=$full; zh=$zh; en=$en; message=($zh+' / '+$en).Trim(' ','/') }
}

function Assert-D18GameDirectoryScope {
    param([string]$Path)
    $scope = Get-D18GameDirectoryScope $Path
    if (-not $scope.allowed) {
        $error = [InvalidOperationException]::new($scope.message)
        $error.Data['D18Scope'] = $scope
        throw $error
    }
    return $scope.path
}
