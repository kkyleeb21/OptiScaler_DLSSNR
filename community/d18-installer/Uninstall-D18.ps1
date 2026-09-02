#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameDir,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
. (Join-Path $PSScriptRoot 'D18-Common.ps1')

try {
    if ([string]::IsNullOrWhiteSpace($GameDir)) {
        $GameDir = Read-Host 'Game directory used for the D18 installation'
    }
    if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) {
        throw "Game directory does not exist: $GameDir"
    }
    $game = (Resolve-Path -LiteralPath $GameDir).Path.TrimEnd('\')
    $statePath = Join-Path $game '.dlssnr-d18-install.json'
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        throw "No managed D18 installation state was found: $statePath"
    }

    $state = Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
    if ($state.format -ne 'dlssnr-d18-install-state-v1') {
        throw "Unsupported installation state: $($state.format)"
    }
    $backupRoot = Join-Path $game ([string]$state.backup_relative)
    if (-not (Test-Path -LiteralPath $backupRoot -PathType Container)) {
        throw "The D18 backup directory is missing: $backupRoot"
    }

    Write-Host "Restore the pre-D18 state in: $game"
    Write-Host "Backup source: $backupRoot"
    if (-not (Confirm-D18Choice -Prompt 'Continue with exact-file uninstall?' -AssumeYes:$Yes)) {
        throw 'Uninstall cancelled by user.'
    }

    $changedAfterInstall = Join-Path $backupRoot 'post-install-user-files'
    $records = @($state.files)
    for ($i = $records.Count - 1; $i -ge 0; $i--) {
        $record = $records[$i]
        $relative = [string]$record.target_relative
        if ([System.IO.Path]::IsPathRooted($relative) -or $relative -match '(^|[\\/])\.\.([\\/]|$)') {
            throw "Unsafe path in installation state: $relative"
        }
        $target = [System.IO.Path]::GetFullPath((Join-Path $game $relative))
        $prefix = $game.TrimEnd('\') + '\'
        if (-not $target.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Installation state escapes the game folder: $relative"
        }

        if (Test-Path -LiteralPath $target -PathType Leaf) {
            $currentHash = Get-D18Sha256 -LiteralPath $target
            if ($currentHash -ne [string]$record.installed_sha256) {
                $preservePath = Join-Path $changedAfterInstall $relative
                $preserveParent = Split-Path -Parent $preservePath
                New-Item -ItemType Directory -Path $preserveParent -Force | Out-Null
                Copy-Item -LiteralPath $target -Destination $preservePath -Force
                Write-Host "Preserved a post-install modified file: $preservePath" -ForegroundColor Yellow
            }
        }

        if ([bool]$record.existed) {
            $backup = Join-Path $backupRoot ([string]$record.backup_relative)
            if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
                throw "Backup file is missing: $backup"
            }
            $parent = Split-Path -Parent $target
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
            Copy-Item -LiteralPath $backup -Destination $target -Force
        }
        elseif (Test-Path -LiteralPath $target -PathType Leaf) {
            Remove-Item -LiteralPath $target -Force
        }
    }

    Remove-Item -LiteralPath $statePath -Force
    Write-Host ''
    Write-Host 'D18 uninstalled. The timestamped backup was retained for manual recovery.' -ForegroundColor Green
    Write-Host "Backup retained at: $backupRoot"
    exit 0
}
catch {
    Write-Host ''
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
