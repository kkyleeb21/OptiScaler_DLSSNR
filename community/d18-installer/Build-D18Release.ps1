#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$D18PackageInstallRoot,
    # Parent directory for the canonical DLSSNR_D18_<version> folder and ZIP.
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$DocsRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
. (Join-Path $PSScriptRoot 'D18-Common.ps1')

$patchGenerator = Join-Path $PSScriptRoot 'runtime_patch_source\Generate-D18RuntimePatch.ps1'
if (-not (Test-Path -LiteralPath $patchGenerator -PathType Leaf)) {
    throw "Readable Runtime patch generator was not found: $patchGenerator"
}
& $patchGenerator -Check

$versionPath = Join-Path $PSScriptRoot 'VERSION'
if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    throw "D18 version file was not found: $versionPath"
}
$releaseVersion = [System.IO.File]::ReadAllText($versionPath).Trim()
if ($releaseVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw "D18 version must use MAJOR.MINOR.PATCH: $releaseVersion"
}
$releaseName = "DLSSNR_D18_$releaseVersion"

$packageRoot = (Resolve-Path -LiteralPath $D18PackageInstallRoot).Path
$outputParent = [System.IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
    New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
}
$output = Join-Path $outputParent $releaseName
$zipPath = "$output.zip"
if ((Test-Path -LiteralPath $output) -or (Test-Path -LiteralPath $zipPath)) {
    throw "Canonical release output already exists; move or remove it before rebuilding: $output"
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sourceCommit = (& git -C $repositoryRoot rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($sourceCommit)) {
    throw 'Unable to resolve the repository commit for release provenance.'
}
$sourceCommit = $sourceCommit.Trim()
$workingTreeState = @(& git -C $repositoryRoot status --porcelain)
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect the repository working tree for release provenance.'
}
if ($workingTreeState.Count -gt 0) {
    $sourceCommit += ' + working tree'
}

$required = @(
    'dxgi.dll',
    'nvngx.dll_dlssnr.dll',
    'OptiScaler.ini',
    'Licenses',
    'OptiScaler'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $relative))) {
        throw "D18 package input is incomplete: $relative"
    }
}

$optiscalerHash = Get-D18Sha256 -LiteralPath (Join-Path $packageRoot 'dxgi.dll')
$forwarderHash = Get-D18Sha256 -LiteralPath (Join-Path $packageRoot 'nvngx.dll_dlssnr.dll')
if ($optiscalerHash -ne 'C3E8F20F5AD48248E78B3B847DB25463C4214D0A81A99C1022452D420CE1A507') {
    throw "Unexpected D18 OptiScaler build: $optiscalerHash"
}
if ($forwarderHash -ne '4B04978A4A5056366E7D13A7A7825CFC2299D14AD029B4F675D664997BDBCB10') {
    throw "Unexpected D18 forwarder build: $forwarderHash"
}

$payload = Join-Path $output 'payload'
New-Item -ItemType Directory -Path $payload -Force | Out-Null

$installerFiles = @(
    'VERSION',
    'D18-Common.ps1',
    'Install-D18.ps1',
    'Install-D18.bat',
    'Uninstall-D18.ps1',
    'Uninstall-D18.bat',
    'runtime_patch.json',
    'README.md',
    'README_CN.md',
    'THIRD_PARTY_NOTICES.md'
)
foreach ($name in $installerFiles) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $output $name) -Force
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'runtime_patch_source') -Destination $output -Recurse -Force
$repoLicense = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\LICENSE'))
if (-not (Test-Path -LiteralPath $repoLicense -PathType Leaf)) {
    throw "Repository GPL license was not found: $repoLicense"
}
Copy-Item -LiteralPath $repoLicense -Destination (Join-Path $output 'LICENSE') -Force

Copy-Item -LiteralPath (Join-Path $packageRoot 'dxgi.dll') -Destination (Join-Path $payload 'OptiScaler.dll') -Force
Copy-Item -LiteralPath (Join-Path $packageRoot 'nvngx.dll_dlssnr.dll') -Destination $payload -Force
Copy-Item -LiteralPath (Join-Path $packageRoot 'Licenses') -Destination $payload -Recurse -Force
Copy-Item -LiteralPath (Join-Path $packageRoot 'OptiScaler') -Destination $payload -Recurse -Force

$iniDestination = Join-Path $payload 'OptiScaler.ini.d18'
Copy-Item -LiteralPath (Join-Path $packageRoot 'OptiScaler.ini') -Destination $iniDestination -Force
$iniText = [System.IO.File]::ReadAllText($iniDestination)
$iniText = [regex]::Replace($iniText, '(?m)^OverrideSharpness=.*$', 'OverrideSharpness=true')
$iniText = [regex]::Replace($iniText, '(?m)^Sharpness=.*$', 'Sharpness=0.85')
[System.IO.File]::WriteAllText($iniDestination, $iniText, [System.Text.UTF8Encoding]::new($false))

if (-not [string]::IsNullOrWhiteSpace($DocsRoot) -and (Test-Path -LiteralPath $DocsRoot -PathType Container)) {
    $docsDestination = Join-Path $output 'docs'
    New-Item -ItemType Directory -Path $docsDestination -Force | Out-Null
    foreach ($name in @('D18_OPTISCALER_012_EXPOSURE_INTEGRATION_CN.md', 'D18_THREE_GAME_VALIDATION_STAGE_REPORT_CN.md')) {
        $source = Join-Path $DocsRoot $name
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination $docsDestination -Force
        }
    }
}

$payloadFiles = @(Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName)
$payloadEntries = foreach ($file in $payloadFiles) {
    [ordered]@{
        path = $file.FullName.Substring($payload.Length + 1)
        size = $file.Length
        sha256 = Get-D18Sha256 -LiteralPath $file.FullName
    }
}
$payloadManifest = [ordered]@{
    format = 'dlssnr-d18-payload-manifest-v1'
    release_name = $releaseName
    release_version = $releaseVersion
    generated_at = (Get-Date).ToString('o')
    contains_nvidia_runtime = $false
    source_commit = $sourceCommit
    files = @($payloadEntries)
}
[System.IO.File]::WriteAllText(
    (Join-Path $output 'payload_manifest.json'),
    ($payloadManifest | ConvertTo-Json -Depth 6),
    [System.Text.UTF8Encoding]::new($false))

$allReleaseFiles = @(Get-ChildItem -LiteralPath $output -Recurse -File | Sort-Object FullName)
$sumLines = foreach ($file in $allReleaseFiles) {
    $relative = $file.FullName.Substring($output.Length + 1)
    "$(Get-D18Sha256 -LiteralPath $file.FullName)  $relative"
}
[System.IO.File]::WriteAllLines((Join-Path $output 'SHA256SUMS.txt'), $sumLines, [System.Text.UTF8Encoding]::new($false))

Compress-Archive -Path (Join-Path $output '*') -DestinationPath $zipPath -CompressionLevel Optimal
Write-Host "Release version: $releaseVersion"
Write-Host "Release folder: $output"
Write-Host "Release ZIP   : $zipPath"
Write-Host "ZIP SHA-256  : $(Get-D18Sha256 -LiteralPath $zipPath)"
