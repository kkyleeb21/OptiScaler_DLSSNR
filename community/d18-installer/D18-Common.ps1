Set-StrictMode -Version 2.0

$script:D18OfficialRuntimeSha256 = 'E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E'
$script:D18PatchedRuntimeSha256 = 'CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414'

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
    if ($patch.format -ne 'dlssnr-d18-guarded-byte-patch-v1') {
        throw "Unsupported Runtime patch format: $($patch.format)"
    }

    $sourceHash = Get-D18Sha256 -LiteralPath $source
    if ($sourceHash -ne $patch.source_sha256 -or $sourceHash -ne $script:D18OfficialRuntimeSha256) {
        throw "Unsupported Runtime. Expected official 310.8 SHA-256 $($patch.source_sha256), got $sourceHash."
    }

    $inputBytes = [System.IO.File]::ReadAllBytes($source)
    if ($inputBytes.LongLength -ne [long]$patch.source_size) {
        throw "Official Runtime size mismatch: expected $($patch.source_size), got $($inputBytes.LongLength)."
    }

    $outputBytes = New-Object byte[] ([int]$patch.output_size)
    [System.Array]::Copy($inputBytes, 0, $outputBytes, 0, $inputBytes.Length)

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

        if (-not (Test-D18BytesAtOffset -Buffer $inputBytes -Offset $offset -Expected $expected)) {
            throw ('Runtime guard failed at file offset 0x{0:X}.' -f $offset)
        }
        if ($offset + $replacement.Length -gt $outputBytes.LongLength) {
            throw ('Runtime replacement exceeds output at file offset 0x{0:X}.' -f $offset)
        }
        [System.Array]::Copy($replacement, 0, $outputBytes, $offset, $replacement.Length)
    }

    $outputDirectory = Split-Path -Parent $outputFull
    if (-not (Test-Path -LiteralPath $outputDirectory)) {
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    }

    $temporary = "$outputFull.tmp-$([guid]::NewGuid().ToString('N'))"
    try {
        [System.IO.File]::WriteAllBytes($temporary, $outputBytes)
        $outputHash = Get-D18Sha256 -LiteralPath $temporary
        if ($outputHash -ne $patch.output_sha256 -or $outputHash -ne $script:D18PatchedRuntimeSha256) {
            throw "Patched Runtime verification failed: expected $($patch.output_sha256), got $outputHash."
        }
        Move-Item -LiteralPath $temporary -Destination $outputFull -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }

    return $outputFull
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
