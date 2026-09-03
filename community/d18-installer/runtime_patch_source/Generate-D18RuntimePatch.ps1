#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$SpecPath,
    [string]$OutputPath,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

if ([string]::IsNullOrWhiteSpace($SpecPath)) {
    $SpecPath = Join-Path $PSScriptRoot 'runtime_patch_spec.json'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $PSScriptRoot '..\runtime_patch.json'
}

function Convert-D18HexToBytes {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Hex)

    $compact = [regex]::Replace($Hex, '[^0-9A-Fa-f]', '')
    if (($compact.Length % 2) -ne 0) {
        throw "Hex text has an odd number of digits: $Hex"
    }
    $bytes = New-Object byte[] ($compact.Length / 2)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [Convert]::ToByte($compact.Substring($i * 2, 2), 16)
    }
    return ,$bytes
}

function Get-D18ReplacementBytes {
    param([Parameter(Mandatory = $true)]$Hunk)

    if ($Hunk.PSObject.Properties.Name -contains 'replacement_hex') {
        return ,([byte[]](Convert-D18HexToBytes -Hex ([string]$Hunk.replacement_hex)))
    }

    $result = New-Object System.Collections.Generic.List[byte]
    foreach ($part in @($Hunk.replacement_parts)) {
        if ($part.PSObject.Properties.Name -contains 'hex') {
            $result.AddRange([byte[]](Convert-D18HexToBytes -Hex ([string]$part.hex)))
        }
        elseif ($part.PSObject.Properties.Name -contains 'fill_byte') {
            $fill = Convert-D18HexToBytes -Hex ([string]$part.fill_byte)
            if ($fill.Length -ne 1 -or [int]$part.count -lt 0) {
                throw "Invalid fill part in hunk at offset $($Hunk.offset)."
            }
            for ($i = 0; $i -lt [int]$part.count; $i++) {
                $result.Add($fill[0])
            }
        }
        else {
            throw "Unknown replacement part in hunk at offset $($Hunk.offset)."
        }
    }
    return ,$result.ToArray()
}

function Split-D18Base64 {
    param([Parameter(Mandatory = $true)][string]$Text)

    $parts = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $Text.Length; $i += 80) {
        $parts.Add($Text.Substring($i, [Math]::Min(80, $Text.Length - $i)))
    }
    return $parts.ToArray()
}

$spec = Get-Content -Raw -LiteralPath $SpecPath | ConvertFrom-Json
if ($spec.format -ne 'dlssnr-d18-readable-patch-spec-v1') {
    throw "Unsupported readable patch spec: $($spec.format)"
}

$generatedHunks = foreach ($hunk in $spec.hunks) {
    $generated = [ordered]@{
        offset = [long]$hunk.offset
        expected_base64 = [Convert]::ToBase64String([byte[]](Convert-D18HexToBytes -Hex ([string]$hunk.expected_hex)))
    }
    if ($hunk.PSObject.Properties.Name -contains 'compatible_input_hex') {
        $generated.compatible_input_base64 = @(
            foreach ($variant in @($hunk.compatible_input_hex)) {
                [Convert]::ToBase64String([byte[]](Convert-D18HexToBytes -Hex ([string]$variant)))
            }
        )
    }
    $replacement = [byte[]](Get-D18ReplacementBytes -Hunk $hunk)
    if ($hunk.PSObject.Properties.Name -contains 'replacement_parts') {
        $generated.replacement_base64_parts = @(Split-D18Base64 -Text ([Convert]::ToBase64String($replacement)))
    }
    else {
        $generated.replacement_base64 = [Convert]::ToBase64String($replacement)
    }
    [pscustomobject]$generated
}

$manifest = [ordered]@{
    format = [string]$spec.output_format
    runtime = [string]$spec.runtime
    reference_source_size = [long]$spec.reference_source_size
    reference_output_size = [long]$spec.reference_output_size
    reference_source_sha256 = [string]$spec.reference_source_sha256
    reference_output_sha256 = [string]$spec.reference_output_sha256
    notice = [string]$spec.notice
    hunks = @($generatedHunks)
}

$generatedJson = $manifest | ConvertTo-Json -Depth 10
if ($Check) {
    if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
        throw "Generated manifest target is missing: $OutputPath"
    }
    $current = Get-Content -Raw -LiteralPath $OutputPath | ConvertFrom-Json
    $currentCanonical = $current | ConvertTo-Json -Depth 10 -Compress
    $generatedCanonical = ([pscustomobject]$manifest) | ConvertTo-Json -Depth 10 -Compress
    if ($currentCanonical -cne $generatedCanonical) {
        throw 'runtime_patch.json does not match runtime_patch_source/runtime_patch_spec.json. Regenerate it before release.'
    }
    Write-Host 'Runtime patch manifest matches the readable source specification.' -ForegroundColor Green
    return
}

[System.IO.File]::WriteAllText(
    [System.IO.Path]::GetFullPath($OutputPath),
    $generatedJson + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))
Write-Host "Generated Runtime patch manifest: $([System.IO.Path]::GetFullPath($OutputPath))"
