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

function Get-D18InstallSpacePreflight {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedGameDir,
        [Parameter(Mandatory = $true)]$InstallItems
    )

    [long]$installBytes = 0
    [long]$backupBytes = 0
    foreach ($item in $InstallItems) {
        $sourceInfo = Get-Item -LiteralPath $item.Source
        $installBytes += [long]$sourceInfo.Length

        $target = Assert-D18TargetInsideGame -ResolvedGameDir $ResolvedGameDir -RelativePath $item.TargetRelative
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            $backupBytes += [long](Get-Item -LiteralPath $target).Length
        }
    }

    # Leave room for the state JSON, directory metadata, and filesystem allocation variance.
    [long]$headroomBytes = 64MB
    [long]$requiredBytes = $installBytes + $backupBytes + $headroomBytes
    $root = [System.IO.Path]::GetPathRoot($ResolvedGameDir)
    $drive = New-Object System.IO.DriveInfo($root)
    [long]$availableBytes = $drive.AvailableFreeSpace
    if ($availableBytes -lt $requiredBytes) {
        throw (('Insufficient free space on {0}: need at least {1:N0} MiB, available {2:N0} MiB. ' +
                'No existing D18 installation was changed.') -f
            $root, ($requiredBytes / 1MB), ($availableBytes / 1MB))
    }

    return [pscustomobject]@{
        InstallBytes = $installBytes
        BackupBytes = $backupBytes
        HeadroomBytes = $headroomBytes
        RequiredBytes = $requiredBytes
        AvailableBytes = $availableBytes
        DriveRoot = $root
    }
}

$game = $null
$statePath = $null
$backupRoot = $null
$records = New-Object System.Collections.Generic.List[object]
$installationStarted = $false
$patchedTemp = $null
$existingManagedInstall = $false
$existingInstallRemoved = $false

try {
    if (-not (Test-Path -LiteralPath $payloadRoot -PathType Container) -or
        -not (Test-Path -LiteralPath $payloadManifestPath -PathType Leaf)) {
        throw 'This is a source checkout, not a complete release. Build or download the community Release ZIP first.'
    }
    $payloadManifest = Test-D18Payload -PayloadRoot $payloadRoot -ManifestPath $payloadManifestPath
    $releaseName = if ($payloadManifest.PSObject.Properties.Name -contains 'release_name') {
        [string]$payloadManifest.release_name
    }
    else {
        'DLSSNR D18 (unversioned package)'
    }
    $releaseVersion = if ($payloadManifest.PSObject.Properties.Name -contains 'release_version') {
        [string]$payloadManifest.release_version
    }
    else {
        $null
    }
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
    $existingManagedInstall = Test-Path -LiteralPath $statePath -PathType Leaf
    if ($existingManagedInstall) {
        Write-Host ''
        Write-Host 'An existing managed D18 installation was detected.' -ForegroundColor Yellow
        Write-Host "  State file   : $statePath"
        Write-Host 'The installer can safely uninstall it, retain its timestamped backup, and install this package.'
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

    # Finish every fallible package preparation step before asking to replace an existing
    # managed install. In particular, an incompatible Runtime must never uninstall a working D18.
    $patchedTemp = Join-Path ([System.IO.Path]::GetTempPath()) ("nvngx_dlssnr.d18.$([guid]::NewGuid().ToString('N')).dll")
    $runtimeResult = New-D18PatchedRuntime -SourcePath $runtimeSource -OutputPath $patchedTemp -PatchManifest $runtimePatchPath

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

    # Resolve every destination and reject duplicate mappings before any uninstall or copy.
    $targetSet = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($item in $installItems) {
        $target = Assert-D18TargetInsideGame -ResolvedGameDir $game -RelativePath $item.TargetRelative
        if (-not $targetSet.Add($target)) {
            throw "Duplicate payload destination: $($item.TargetRelative)"
        }
        if ((Get-D18Sha256 -LiteralPath $item.Source) -ne $item.ExpectedHash) {
            throw "Prepared source verification failed for $($item.TargetRelative)."
        }
    }
    $spacePreflight = Get-D18InstallSpacePreflight -ResolvedGameDir $game -InstallItems $installItems

    $proxyTarget = Join-Path $game $ProxyName
    if (-not $existingManagedInstall -and (Test-Path -LiteralPath $proxyTarget -PathType Leaf)) {
        Write-Host "Existing $ProxyName will be backed up, but replacing an existing ReShade/mod loader may break its chain." -ForegroundColor Yellow
    }

    Write-Host ''
    Write-Host 'D18 installation summary'
    Write-Host "  Package     : $releaseName"
    Write-Host "  Game folder : $game"
    Write-Host "  Proxy name  : $ProxyName"
    Write-Host "  Runtime     : $runtimeSource"
    Write-Host "  Input SHA256: $runtimeSourceHash"
    Write-Host "  Output SHA256: $($runtimeResult.OutputSha256)"
    Write-Host "  Runtime hunks: $($runtimeResult.AppliedHunks) applied ($($runtimeResult.CompatibleVariantHunks) compatibility variants), $($runtimeResult.AlreadyPatchedHunks) already present"
    Write-Host ("  Disk preflight: {0:N0} MiB required, {1:N0} MiB available on {2}" -f
        ($spacePreflight.RequiredBytes / 1MB), ($spacePreflight.AvailableBytes / 1MB), $spacePreflight.DriveRoot)
    Write-Host '  Network     : 0.5 internal ratio (4K -> exact 1920x1080)'
    Write-Host '  Input filter: Custom Mitchell'
    Write-Host '  Runtime gate: guarded D18 byte ranges; full-file hash is recorded, not allowlisted'
    Write-Host '  NVIDIA DLL  : patched locally; no Runtime binary came with this package'
    if ($existingManagedInstall) {
        if (-not (Confirm-D18Choice -Prompt 'Replace the existing managed D18 installation using safe uninstall/reinstall?' -AssumeYes:$Yes)) {
            throw 'Replacement cancelled by user. The existing D18 installation was not changed.'
        }

        $uninstallerPath = Join-Path $PSScriptRoot 'Uninstall-D18.ps1'
        $windowsPowerShell = Join-Path ([Environment]::GetFolderPath('System')) 'WindowsPowerShell\v1.0\powershell.exe'
        if (-not (Test-Path -LiteralPath $uninstallerPath -PathType Leaf) -or
            -not (Test-Path -LiteralPath $windowsPowerShell -PathType Leaf)) {
            throw 'The bundled D18 uninstaller or Windows PowerShell 5.1 could not be found.'
        }

        Write-Host ''
        Write-Host 'Safely removing the existing managed D18 installation before replacement...' -ForegroundColor Yellow
        & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $uninstallerPath -GameDir $game -Yes
        if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $statePath -PathType Leaf)) {
            throw 'Existing D18 uninstall failed. The replacement installation was not started.'
        }
        $existingInstallRemoved = $true
        Write-Host 'Existing D18 removed; its previous timestamped backup was retained.' -ForegroundColor Green
    }
    elseif (-not (Confirm-D18Choice -Prompt 'Install and create a recoverable backup?' -AssumeYes:$Yes)) {
        throw 'Installation cancelled by user.'
    }

    try {
        $timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
        $backupParent = Join-Path $game 'D18_Backups'
        $backupRoot = Join-Path $backupParent $timestamp
        $backupSuffix = 1
        while (Test-Path -LiteralPath $backupRoot) {
            $backupRoot = Join-Path $backupParent ("{0}_{1:D2}" -f $timestamp, $backupSuffix)
            $backupSuffix++
        }
        $backupFiles = Join-Path $backupRoot 'files'
        New-Item -ItemType Directory -Path $backupFiles -Force | Out-Null
        $installationStarted = $true

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
            package_name = $releaseName
            package_version = $releaseVersion
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
    if ($patchedTemp -and (Test-Path -LiteralPath $patchedTemp)) {
        Remove-Item -LiteralPath $patchedTemp -Force -ErrorAction SilentlyContinue
    }
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
    elseif ($existingInstallRemoved) {
        Write-Host 'The previous D18 installation remains safely uninstalled; its old timestamped backup is still available.' -ForegroundColor Yellow
    }
    exit 1
}
