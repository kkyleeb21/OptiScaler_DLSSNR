#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameDir,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
. (Join-Path $PSScriptRoot 'D18-Common.ps1')

function Resolve-D18ContainedPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [System.IO.Path]::IsPathRooted($RelativePath) -or
        $RelativePath -match '(^|[\\/])\.\.([\\/]|$)') {
        throw "Unsafe $Description path in installation state: $RelativePath"
    }

    $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $resolved = [System.IO.Path]::GetFullPath((Join-Path $resolvedRoot $RelativePath))
    if (-not $resolved.StartsWith($resolvedRoot + '\',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description path escapes its allowed root: $RelativePath"
    }
    return $resolved
}

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

    if ($state.PSObject.Properties.Name -contains 'game_dir') {
        $recordedGame = [System.IO.Path]::GetFullPath([string]$state.game_dir).TrimEnd('\')
        if (-not [string]::Equals($recordedGame, $game, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Installation state belongs to a different game directory: $recordedGame"
        }
    }

    $backupRoot = Resolve-D18ContainedPath -Root $game `
        -RelativePath ([string]$state.backup_relative) -Description 'backup root'
    $backupContainer = [System.IO.Path]::GetFullPath((Join-Path $game 'D18_Backups')).TrimEnd('\')
    if (-not $backupRoot.StartsWith($backupContainer + '\',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Backup root is outside the managed D18_Backups directory: $backupRoot"
    }
    if (-not (Test-Path -LiteralPath $backupRoot -PathType Container)) {
        throw "The D18 backup directory is missing: $backupRoot"
    }

    # Compile the complete recovery plan before confirmation. A malformed state or missing late
    # backup must be rejected before the first preserve, restore, or delete operation.
    $records = @($state.files)
    if ($records.Count -eq 0) {
        throw 'The installation state contains no managed files.'
    }
    $targetSet = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $restorePlan = New-Object System.Collections.Generic.List[object]
    foreach ($record in $records) {
        $relative = [string]$record.target_relative
        $target = Resolve-D18ContainedPath -Root $game -RelativePath $relative -Description 'target'
        if (-not $targetSet.Add($target)) {
            throw "Duplicate target in installation state: $relative"
        }

        $backup = $null
        if ([bool]$record.existed) {
            $backup = Resolve-D18ContainedPath -Root $backupRoot `
                -RelativePath ([string]$record.backup_relative) -Description 'backup file'
            if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
                throw "Backup file is missing: $backup"
            }
        }

        $restorePlan.Add([pscustomobject]@{
            Record = $record
            Relative = $relative
            Target = $target
            Backup = $backup
        })
    }

    Write-Host "Restore the pre-D18 state in: $game"
    Write-Host "Backup source: $backupRoot"
    Write-Host "Preflight verified: $($restorePlan.Count) unique targets and every required backup."
    if (-not (Confirm-D18Choice -Prompt 'Continue with exact-file uninstall?' -AssumeYes:$Yes)) {
        throw 'Uninstall cancelled by user.'
    }

    $changedAfterInstall = Join-Path $backupRoot 'post-install-user-files'
    for ($i = $restorePlan.Count - 1; $i -ge 0; $i--) {
        $item = $restorePlan[$i]
        $record = $item.Record
        $relative = $item.Relative
        $target = $item.Target

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
            $parent = Split-Path -Parent $target
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
            Copy-Item -LiteralPath $item.Backup -Destination $target -Force
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
