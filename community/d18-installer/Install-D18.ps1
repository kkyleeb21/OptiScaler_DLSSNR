#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameDir,
    [string]$RuntimePath,
    [ValidateSet('dxgi.dll', 'winmm.dll', 'version.dll', 'dbghelp.dll', 'd3d12.dll')]
    [string]$ProxyName,
    [ValidateSet('Auto','None','DX11','Vulkan')][string]$NativeApi = 'Auto',
    [switch]$Yes,
    [switch]$AcknowledgeAntiCheatRisk,
    [string]$UiToggleKey,
    [ValidateSet('Auto','Recommended','Latest','Existing','Manual')][string]$REFramework = 'Auto',
    [switch]$REEngine
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
. (Join-Path $PSScriptRoot 'D18-Common.ps1')
. (Join-Path $PSScriptRoot 'D18-REFramework.ps1')
$refStage = $null

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
        if (-not (Test-Path -LiteralPath $Requested -PathType Leaf)) { throw "Runtime file does not exist: $Requested" }
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
    $nativeRuntime = Join-Path $ResolvedGameDir 'D24Runtime.dll'
    if (Test-Path -LiteralPath $nativeRuntime -PathType Leaf) {
        # Prefer the installed native runtime over a leftover DX12 runtime when upgrading native NR.
        if ($NativeApi -in @('DX11','Vulkan')) { $candidates.Insert([Math]::Max(0,$candidates.Count - $(if(Test-Path -LiteralPath $gameRuntime -PathType Leaf){1}else{0})), $nativeRuntime) }
        else { $candidates.Add($nativeRuntime) }
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    if ($Yes) { throw 'Runtime not found. Supply -RuntimePath or put nvngx_dlssnr.dll in runtime_input beside the installer.' }
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
    # A mirrored directory must not redirect writes outside the chosen game directory.
    $cursor = $target
    while ($cursor.Length -ge $ResolvedGameDir.Length) {
        if ((Test-Path -LiteralPath $cursor) -and
            ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Resolve the linked deployment path before installing: $cursor"
        }
        $cursor = Split-Path -Parent $cursor
        if (-not $cursor) { break }
    }
    return $target
}

function New-D18UpgradeRecovery {
    param([string]$Game, [string]$StatePath)
    $old = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
    $names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $paths = @($old.files | ForEach-Object { [string]$_.target_relative }) + '.dlssnr-d18-install.json'
    $plan = @()
    foreach ($relative in $paths) {
        $target = Assert-D18TargetInsideGame -ResolvedGameDir $Game -RelativePath $relative
        if (-not $names.Add($target)) { throw "Duplicate upgrade recovery target: $relative" }
        if ((Test-Path -LiteralPath $target) -and -not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Recovery target is not a file: $target" }
        $plan += [pscustomobject]@{ relative=$relative; exists=(Test-Path -LiteralPath $target -PathType Leaf); sha256=$null }
    }
    $relativeRoot='D18_Backups\upgrade-recovery-'+[guid]::NewGuid().ToString('N')
    $root=Assert-D18TargetInsideGame -ResolvedGameDir $Game -RelativePath $relativeRoot
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    foreach ($entry in $plan) {
        if (-not $entry.exists) { continue }
        $target=Join-Path $Game $entry.relative
        $copy=Join-Path $root $entry.relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
        $entry.sha256=Get-D18Sha256 $target
        Copy-Item -LiteralPath $target -Destination $copy -Force
        if ((Get-D18Sha256 $copy) -ne $entry.sha256) { throw "Upgrade recovery copy mismatch: $target" }
    }
    $recovery=[pscustomobject]@{ root=$root; files=$plan }
    $recovery | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'recovery.json') -Encoding UTF8
    return $recovery
}

function Restore-D18UpgradeRecovery {
    param([string]$Game, $Recovery)
    # Validate all backup bytes and targets before restoring; restore the install state last.
    foreach ($entry in $Recovery.files) {
        $null=Assert-D18TargetInsideGame -ResolvedGameDir $Game -RelativePath $entry.relative
        if ($entry.exists -and (Get-D18Sha256 (Join-Path $Recovery.root $entry.relative)) -ne $entry.sha256) { throw "Recovery backup mismatch: $($entry.relative)" }
    }
    foreach ($entry in $Recovery.files) {
        $target=Join-Path $Game $entry.relative
        if ($entry.exists) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $Recovery.root $entry.relative) -Destination $target -Force
            if ((Get-D18Sha256 $target) -ne $entry.sha256) { throw "Recovery verification failed: $target" }
        } elseif (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force }
    }
    Write-Host 'Previous D18 installation and settings restored and hash-verified.' -ForegroundColor Green
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
    [long]$recoveryBytes = 0
    if ($existingManagedInstall) {
        foreach ($entry in $prior.files) {
            $target=Assert-D18TargetInsideGame -ResolvedGameDir $ResolvedGameDir -RelativePath ([string]$entry.target_relative)
            if(Test-Path -LiteralPath $target -PathType Leaf){$recoveryBytes += [long](Get-Item -LiteralPath $target).Length}
        }
    }
    [long]$requiredBytes = $installBytes + $backupBytes + $recoveryBytes + $headroomBytes
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
$upgradeRecovery = $null
$upgradeMutationStarted = $false
$profileTemps = New-Object System.Collections.Generic.List[string]

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
    Assert-D18GameStopped $game
    $reProfile = Get-D18ReProfile -Game $game -ForceRE:$REEngine
    $previousProxy = $null
    $askNativeApi = $NativeApi -eq 'Auto'
    $priorState = Join-Path $game $stateFileName
    if (Test-Path -LiteralPath $priorState) {
        $prior = Get-Content -LiteralPath $priorState -Raw | ConvertFrom-Json
        $previousProxy = [string]$prior.proxy_name
        if ($NativeApi -eq 'Auto' -and $prior.PSObject.Properties.Name -contains 'native_api') { $NativeApi = [string]$prior.native_api }
    }
    $recommendation = Get-D18ProxyRecommendation -Game $game -IsRE $reProfile.IsRE
    Write-Host $recommendation.Reason
    $ProxyName = Select-D18ProxyName -Requested $ProxyName -Previous $previousProxy -Recommended $recommendation.Name -AssumeYes:$Yes
    if ($NativeApi -eq 'Auto') {
        $NativeApi = if (Test-Path -LiteralPath (Join-Path $game 'nioh2.exe')) { 'DX11' }
                    elseif (Test-Path -LiteralPath (Join-Path $game 'DOOMTheDarkAges.exe')) { 'Vulkan' }
                    else { 'None' }
    }
        if ($askNativeApi -and -not $Yes -and -not $reProfile.IsRE) {
            Write-Host 'Select the graphics API you will use in the game: 1 = DX12, 2 = DX11, 3 = Vulkan.'
            do {
                $apiChoice = Read-Host "Press Enter to keep $NativeApi, or choose 1 / 2 / 3"
            } while ($apiChoice -notin @('', '1', '2', '3'))
            if ($apiChoice -ne '') { $NativeApi = @{ '1'='None'; '2'='DX11'; '3'='Vulkan' }[$apiChoice] }
        }
    $statePath = Join-Path $game $stateFileName
    $existingManagedInstall = Test-Path -LiteralPath $statePath -PathType Leaf
    $existingUiIni = Join-Path $game 'OptiScaler.ini'
    $freshUiInstall = -not $existingManagedInstall -and -not (Test-Path -LiteralPath $existingUiIni -PathType Leaf)
    $selectedUiKey = 'auto'
    if ($freshUiInstall) {
        if ($PSBoundParameters.ContainsKey('UiToggleKey')) {
            $selectedUiKey = [string](ConvertTo-D18UiKey $UiToggleKey)
        }
        elseif ($Yes) { $selectedUiKey = '45' }
        else { $selectedUiKey = '45' }
    }
    elseif (Test-Path -LiteralPath $existingUiIni -PathType Leaf) {
        $selectedUiKey = Get-D18UiKey ([IO.File]::ReadAllText($existingUiIni))
    }
    if (-not $freshUiInstall -and $PSBoundParameters.ContainsKey('UiToggleKey')) {
        Write-Host 'Existing installation/config: -UiToggleKey ignored; existing settings are preserved.'
    }
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
    if ($NativeApi -eq 'DX11' -and $runtimeResult.OutputSha256 -ne 'CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414') {
        throw 'This DX11 addon requires the verified 310.8 Runtime. The supplied Runtime can be patched but is not accepted by the DX11 backend. Current installation has not been changed.'
    }
    switch ($runtimeResult.Classification) {
        'VERIFIED' {
            Write-Host '[VERIFIED] Recognized verified Runtime. Continue installation.' -ForegroundColor Green
        }
        'ALREADY_PATCHED' {
            Write-Host '[ALREADY_PATCHED] All required D18 patch bytes are present. No repeat patch is needed; continue installing other components.' -ForegroundColor Green
            Write-Host 'This identifies the patch, not the origin or compatibility of the entire file.'
        }
        'UNVERIFIED_COMPATIBLE' {
            Write-Host '[UNVERIFIED_COMPATIBLE] This Runtime has NOT been verified, but no conflict with the installer patch requirements was found. It may work.' -ForegroundColor Yellow
            Write-Host 'The original will be backed up before replacement. If the game fails, retry with a verified Runtime.' -ForegroundColor Yellow
        }
    }

    $installItems = New-Object System.Collections.Generic.List[object]
    foreach ($entry in $payloadManifest.files) {
        $sourceRelative = [string]$entry.path
        if ($sourceRelative -ieq 'D24Native.dll' -and $NativeApi -ne 'DX11') { continue }
        if ($sourceRelative -ieq 'D24VulkanNR.enabled' -and $NativeApi -ne 'Vulkan') { continue }
        $targetRelative = Get-D18TargetRelativePath -PayloadRelativePath $sourceRelative -ProxyName $ProxyName
        $installItems.Add([pscustomobject]@{
            Source = Join-Path $payloadRoot $sourceRelative
            TargetRelative = $targetRelative
            ExpectedHash = [string]$entry.sha256
        })
    }
    $installItems.Add([pscustomobject]@{
        Source = $patchedTemp
        TargetRelative = $(if($NativeApi -in @('DX11','Vulkan')){'D24Runtime.dll'}else{'nvngx_dlssnr.dll'})
        ExpectedHash = $runtimeResult.OutputSha256
    })

    if ($NativeApi -eq 'DX11' -and -not ($installItems | Where-Object TargetRelative -eq 'D24Native.dll')) { throw 'DX11 requires the matching D24Native.dll payload.' }
    if ($NativeApi -eq 'Vulkan' -and -not ($installItems | Where-Object TargetRelative -eq 'D24VulkanNR.enabled')) { throw 'Vulkan requires the native activation payload.' }
    # Preserve the complete existing INI before uninstall, including per-game forwarding and NR settings.
    foreach ($item in $installItems) {
        if ($item.TargetRelative -ieq 'OptiScaler.ini') {
            $iniSource = if (Test-Path -LiteralPath $existingUiIni -PathType Leaf) { $existingUiIni } else { $item.Source }
            $temp = Join-Path ([IO.Path]::GetTempPath()) ('d18-uikey-'+[guid]::NewGuid().ToString('N')+'.ini')
            $profileTemps.Add($temp)
            $text = [IO.File]::ReadAllText($iniSource)
            if ($freshUiInstall) { $text = Set-D18UiKey -Text $text -Value $selectedUiKey }
            if ($NativeApi -in @('DX11','Vulkan')) {
                $routeKey = if ($NativeApi -eq 'DX11') { 'Dx11Upscaler' } else { 'VulkanUpscaler' }
                # Selecting native NR authorizes its two required routing settings; preserve everything else.
                $beforeRoute=$text
                $text = Set-D18IniValue -Text $text -Section 'Upscalers' -Key $routeKey -Value 'dlss'
                $text = Set-D18IniValue -Text $text -Section 'DLSS' -Key 'Enabled' -Value 'true'
                if($text -cne $beforeRoute){Write-Host "Native $NativeApi NR requires [Upscalers] $routeKey=dlss and [DLSS] Enabled=true. These settings will be aligned; other settings are retained." -ForegroundColor Yellow}
                if($freshUiInstall){$text = Set-D18IniValue -Text $text -Section 'DlssNr' -Key 'ToggleKey' -Value '33'}
            }
            [IO.File]::WriteAllText($temp,$text,[Text.UTF8Encoding]::new($false))
            $item.Source = $temp
            $item.ExpectedHash = Get-D18Sha256 $temp
        }
    }

    $refStage = Join-Path ([IO.Path]::GetTempPath()) ('d18-ref-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $refStage | Out-Null
    Add-D18RefItems -Game $game -Profile $reProfile -Mode $REFramework -Stage $refStage -Items $installItems -AssumeYes:$Yes

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
    Write-Host "  UI toggle   : $selectedUiKey (Windows key code; auto=Insert; existing INI preserved)"
    Write-Host "  Runtime     : $runtimeSource"
    Write-Host "  Config      : $(Join-Path $game 'OptiScaler.ini')"
    if ($reProfile.IsRE) { Write-Host '  REF menu    : PgDn (D18 menu defaults to Insert)' }
    Write-Host '  Backup and file verification are enabled.'
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
        Assert-D18GameStopped $game
        $upgradeRecovery = New-D18UpgradeRecovery -Game $game -StatePath $statePath
        Write-Host "Previous installation recovery snapshot: $($upgradeRecovery.root)"
        $upgradeMutationStarted = $true
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
        Assert-D18GameStopped $game
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
            native_api = $NativeApi
            backup_relative = $backupRoot.Substring($game.Length + 1)
            input_runtime_sha256 = $runtimeResult.SourceSha256
            runtime_classification = $runtimeResult.Classification
            runtime_recognized_reference = $runtimeResult.RecognizedReference
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
    Write-Host 'Launch the game and open D18 with Insert (or your saved menu key).'
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
    if ($upgradeMutationStarted -and $upgradeRecovery) {
        try { Restore-D18UpgradeRecovery -Game $game -Recovery $upgradeRecovery }
        catch { Write-Host "Automatic recovery failed: $($_.Exception.Message). Recovery files and manifest: $($upgradeRecovery.root)" -ForegroundColor Red }
    }
    exit 1
}
finally {
    if ($refStage -and (Test-Path -LiteralPath $refStage)) {
        # Only generated flat files are removed; no recursive traversal of user paths.
        Get-ChildItem -LiteralPath $refStage -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
        Remove-Item -LiteralPath $refStage -ErrorAction SilentlyContinue
    }
    foreach ($temp in $profileTemps) {
        if (Test-Path -LiteralPath $temp -PathType Leaf) { Remove-Item -LiteralPath $temp -Force }
    }
}
