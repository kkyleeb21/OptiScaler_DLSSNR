#Requires -Version 5.1
[CmdletBinding()]
param([string]$GameDir,[switch]$Yes,[switch]$PlanOnly,[string]$StateFile,
      [switch]$Manual,[string[]]$ManualFiles=@())
$ErrorActionPreference='Stop'
try {
    & (Join-Path $PSScriptRoot 'D18-Uninstall.ps1') @PSBoundParameters
    exit $LASTEXITCODE
} catch { Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red; exit 1 }
