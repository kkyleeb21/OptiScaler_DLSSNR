# Compatibility entry point; all API adapters share the same collector.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$OutputDirectory,
    [string]$BuildManifest,
    [string]$LayerCaptureDirectory
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'collect-diagnostics.ps1') @PSBoundParameters
