Set-StrictMode -Version 2.0

function Get-D18Sha256 {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    return (Get-FileHash -Algorithm SHA256 -LiteralPath $LiteralPath).Hash.ToUpperInvariant()
}

function Test-D18BytesAtOffset {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Buffer,
        [Parameter(Mandatory = $true)][long]$Offset,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][byte[]]$Expected
    )

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
            throw (('Runtime layout guard failed at file offset 0x{0:X}. This 310.8-based variant ' +
                    'changes bytes required by D18 and cannot be patched automatically.') -f $offset)
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

    return [pscustomobject]@{
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
