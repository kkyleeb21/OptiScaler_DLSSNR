#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameDir,
    [string]$RuntimePath,
    [ValidateSet('dxgi.dll', 'winmm.dll', 'version.dll', 'dbghelp.dll')]
    [string]$ProxyName,
    [switch]$Yes,
    [switch]$AcknowledgeAntiCheatRisk
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
. (Join-Path $PSScriptRoot 'D18-Common.ps1')

$payloadRoot = Join-Path $PSScriptRoot 'payload'
$payloadManifestPath = Join-Path $PSScriptRoot 'payload_manifest.json'
$runtimePatchPath = Join-Path $PSScriptRoot 'runtime_patch.json'
$stateFileName = '.dlssnr-d18-install.json'

function Resolve-D18GameDirectory {
    param([string]$Requested)

    $candidate = $Requested
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = Read-Host 'Game directory containing the game executable'
    }
    if ([string]::IsNullOrWhiteSpace($candidate) -or -not (Test-Path -LiteralPath $candidate -PathType Container)) {
        throw "Game directory does not exist: $candidate"
    }
    return (Resolve-Path -LiteralPath $candidate).Path.TrimEnd('\')
}

function Resolve-D18RuntimeSource {
    param(
        [string]$Requested,
        [string]$ResolvedGameDir
    )

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        $candidates.Add($Requested)
    }
    $localInput = Join-Path $PSScriptRoot 'runtime_input\nvngx_dlssnr.dll'
    if (Test-Path -LiteralPath $localInput -PathType Leaf) {
        $candidates.Add($localInput)
    }
    $gameRuntime = Join-Path $ResolvedGameDir 'nvngx_dlssnr.dll'
    if (Test-Path -LiteralPath $gameRuntime -PathType Leaf) {
        $candidates.Add($gameRuntime)
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $manual = Read-Host 'Path to your 310.8-based nvngx_dlssnr.dll (official or community compatibility build; not included)'
    if ([string]::IsNullOrWhiteSpace($manual) -or -not (Test-Path -LiteralPath $manual -PathType Leaf)) {
        throw 'A user-supplied 310.8-based nvngx_dlssnr.dll is required.'
    }
    return (Resolve-Path -LiteralPath $manual).Path
}

function Assert-D18TargetInsideGame {
    param(
        [string]$ResolvedGameDir,
        [string]$RelativePath
    )

    if ([System.IO.Path]::IsPathRooted($RelativePath) -or $RelativePath -match '(^|[\\/])\.\.([\\/]|$)') {
        throw "Unsafe payload target path: $RelativePath"
    }
    $target = [System.IO.Path]::GetFullPath((Join-Path $ResolvedGameDir $RelativePath))
    $prefix = $ResolvedGameDir.TrimEnd('\') + '\'
    if (-not $target.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Payload target escapes the game directory: $RelativePath"
    }
    return $target
}

$game = $null
$statePath = $null
$backupRoot = $null
$records = New-Object System.Collections.Generic.List[object]
$installationStarted = $false

try {
    if (-not (Test-Path -LiteralPath $payloadRoot -PathType Container) -or
        -not (Test-Path -LiteralPath $payloadManifestPath -PathType Leaf)) {
        throw 'This is a source checkout, not a complete release. Build or download the community Release ZIP first.'
    }
    $payloadManifest = Test-D18Payload -PayloadRoot $payloadRoot -ManifestPath $payloadManifestPath
    $game = Resolve-D18GameDirectory -Requested $GameDir
    if ([string]::IsNullOrWhiteSpace($ProxyName)) {
        Write-Host 'Select the OptiScaler proxy name:'
        Write-Host '  1. dxgi.dll (default)'
        Write-Host '  2. winmm.dll'
        Write-Host '  3. version.dll'
        Write-Host '  4. dbghelp.dll'
        $proxyChoice = Read-Host 'Choice [1]'
        switch ($proxyChoice) {
            '2' { $ProxyName = 'winmm.dll' }
            '3' { $ProxyName = 'version.dll' }
            '4' { $ProxyName = 'dbghelp.dll' }
            default { $ProxyName = 'dxgi.dll' }
        }
    }
    $statePath = Join-Path $game $stateFileName
    if (Test-Path -LiteralPath $statePath) {
        throw "D18 is already managed in this folder. Run Uninstall-D18.bat before reinstalling: $statePath"
    }

    Write-Host ''
    Write-Host 'WARNING: OptiScaler injection is not intended for anti-cheat protected or competitive online games.' -ForegroundColor Yellow
    Write-Host 'Using a mod in an online title can cause launch failures or account penalties.' -ForegroundColor Yellow
    $antiCheatSignals = @(Get-ChildItem -LiteralPath $game -Depth 2 -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '(?i)(easyanticheat|anti.?cheat.?expert|ace-base|battleye|xhunter)' } |
        Select-Object -First 8 -ExpandProperty FullName)
    if ($antiCheatSignals.Count -gt 0) {
        Write-Host 'Possible anti-cheat components were found:' -ForegroundColor Yellow
        $antiCheatSignals | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
        if (-not $AcknowledgeAntiCheatRisk) {
            $ack = Read-Host 'Type I UNDERSTAND to continue'
            if ($ack -cne 'I UNDERSTAND') {
                throw 'Installation cancelled because anti-cheat risk was not acknowledged.'
            }
        }
    }

    $runtimeSource = Resolve-D18RuntimeSource -Requested $RuntimePath -ResolvedGameDir $game
    $runtimeSourceHash = Get-D18Sha256 -LiteralPath $runtimeSource

    $proxyTarget = Join-Path $game $ProxyName
    if (Test-Path -LiteralPath $proxyTarget -PathType Leaf) {
        Write-Host "Existing $ProxyName will be backed up, but replacing an existing ReShade/mod loader may break its chain." -ForegroundColor Yellow
    }

    Write-Host ''
    Write-Host 'D18 installation summary'
    Write-Host "  Game folder : $game"
    Write-Host "  Proxy name  : $ProxyName"
    Write-Host "  Runtime     : $runtimeSource"
    Write-Host "  Input SHA256: $runtimeSourceHash"
    Write-Host '  Network     : 0.5 internal ratio (4K -> exact 1920x1080)'
    Write-Host '  Input filter: Custom Mitchell'
    Write-Host '  Runtime gate: guarded D18 byte ranges; full-file hash is recorded, not allowlisted'
    Write-Host '  NVIDIA DLL  : patched locally; no Runtime binary came with this package'
    if (-not (Confirm-D18Choice -Prompt 'Install and create a recoverable backup?' -AssumeYes:$Yes)) {
        throw 'Installation cancelled by user.'
    }

    $patchedTemp = Join-Path ([System.IO.Path]::GetTempPath()) ("nvngx_dlssnr.d18.$([guid]::NewGuid().ToString('N')).dll")
    try {
        $runtimeResult = New-D18PatchedRuntime -SourcePath $runtimeSource -OutputPath $patchedTemp -PatchManifest $runtimePatchPath
        Write-Host "  Output SHA256: $($runtimeResult.OutputSha256)"
        Write-Host "  Runtime hunks : $($runtimeResult.AppliedHunks) applied ($($runtimeResult.CompatibleVariantHunks) compatibility variants), $($runtimeResult.AlreadyPatchedHunks) already present"

        $timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
        $backupRoot = Join-Path $game "D18_Backups\$timestamp"
        $backupFiles = Join-Path $backupRoot 'files'
        New-Item -ItemType Directory -Path $backupFiles -Force | Out-Null
        $installationStarted = $true

        $installItems = New-Object System.Collections.Generic.List[object]
        foreach ($entry in $payloadManifest.files) {
            $sourceRelative = [string]$entry.path
            $targetRelative = Get-D18TargetRelativePath -PayloadRelativePath $sourceRelative -ProxyName $ProxyName
            $installItems.Add([pscustomobject]@{
                Source = Join-Path $payloadRoot $sourceRelative
                TargetRelative = $targetRelative
                ExpectedHash = [string]$entry.sha256
            })
        }
        $installItems.Add([pscustomobject]@{
            Source = $patchedTemp
            TargetRelative = 'nvngx_dlssnr.dll'
            ExpectedHash = $runtimeResult.OutputSha256
        })

        foreach ($item in $installItems) {
            $target = Assert-D18TargetInsideGame -ResolvedGameDir $game -RelativePath $item.TargetRelative
            $existed = Test-Path -LiteralPath $target -PathType Leaf
            $backupRelative = $null
            if ($existed) {
                $backupRelative = Join-Path 'files' $item.TargetRelative
                $backupPath = Join-Path $backupRoot $backupRelative
                $backupParent = Split-Path -Parent $backupPath
                New-Item -ItemType Directory -Path $backupParent -Force | Out-Null
                Copy-Item -LiteralPath $target -Destination $backupPath -Force
            }

            $records.Add([pscustomobject]@{
                target_relative = $item.TargetRelative
                existed = $existed
                backup_relative = $backupRelative
                installed_sha256 = $item.ExpectedHash
            })

            $targetParent = Split-Path -Parent $target
            New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
            Copy-Item -LiteralPath $item.Source -Destination $target -Force
            $installedHash = Get-D18Sha256 -LiteralPath $target
            if ($installedHash -ne $item.ExpectedHash) {
                throw "Post-copy verification failed for $($item.TargetRelative)."
            }
        }

        $state = [ordered]@{
            format = 'dlssnr-d18-install-state-v1'
            installed_at = (Get-Date).ToString('o')
            game_dir = $game
            proxy_name = $ProxyName
            backup_relative = $backupRoot.Substring($game.Length + 1)
            input_runtime_sha256 = $runtimeResult.SourceSha256
            installed_runtime_sha256 = $runtimeResult.OutputSha256
            runtime_input_size = $runtimeResult.SourceSize
            runtime_output_size = $runtimeResult.OutputSize
            runtime_hunks_applied = $runtimeResult.AppliedHunks
            runtime_compatibility_variant_hunks = $runtimeResult.CompatibleVariantHunks
            runtime_hunks_already_present = $runtimeResult.AlreadyPatchedHunks
            files = $records
        }
        $stateJson = $state | ConvertTo-Json -Depth 6
        [System.IO.File]::WriteAllText((Join-Path $backupRoot 'install-state.json'), $stateJson, [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText($statePath, $stateJson, [System.Text.UTF8Encoding]::new($false))
    }
    finally {
        if (Test-Path -LiteralPath $patchedTemp) {
            Remove-Item -LiteralPath $patchedTemp -Force
        }
    }

    Write-Host ''
    Write-Host 'D18 installed and verified.' -ForegroundColor Green
    Write-Host "Backup: $backupRoot"
    Write-Host 'Recommended subjective sharpness range: 0.80-0.90 in the OptiScaler Sharpness panel.'
    exit 0
}
catch {
    Write-Host ''
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    if ($installationStarted -and $game -and $backupRoot) {
        Write-Host 'Rolling back files touched by this attempt...' -ForegroundColor Yellow
        for ($i = $records.Count - 1; $i -ge 0; $i--) {
            $record = $records[$i]
            $target = Join-Path $game ([string]$record.target_relative)
            try {
                if ([bool]$record.existed) {
                    $backup = Join-Path $backupRoot ([string]$record.backup_relative)
                    if (Test-Path -LiteralPath $backup -PathType Leaf) {
                        $parent = Split-Path -Parent $target
                        New-Item -ItemType Directory -Path $parent -Force | Out-Null
                        Copy-Item -LiteralPath $backup -Destination $target -Force
                    }
                }
                elseif (Test-Path -LiteralPath $target -PathType Leaf) {
                    Remove-Item -LiteralPath $target -Force
                }
            }
            catch {
                Write-Host "Rollback warning for $target : $($_.Exception.Message)" -ForegroundColor Yellow
            }
        }
        if ($statePath -and (Test-Path -LiteralPath $statePath)) {
            Remove-Item -LiteralPath $statePath -Force
        }
    }
    exit 1
}
