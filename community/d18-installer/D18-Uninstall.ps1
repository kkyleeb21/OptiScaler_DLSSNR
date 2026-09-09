#Requires -Version 5.1
[CmdletBinding()]
param([string]$GameDir,[switch]$Yes,[switch]$PlanOnly,[string]$StateFile,
      [switch]$Manual,[string[]]$ManualFiles=@())
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
# No dependency on installation/runtime-patching code. Version-independent v1 records.
function Hash([string]$Path){
    $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}
    finally{$stream.Dispose();$sha.Dispose()}
}
function NoLink([string]$Path){
    $current=[IO.Path]::GetFullPath($Path)
    while($current){
        if(Test-Path -LiteralPath $current){
            if((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked path is not supported: $current"}
        }
        $parent=Split-Path -Parent $current
        if($parent -eq $current){break};$current=$parent
    }
}
function Contained([string]$Root,[string]$Relative){
    if([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or
       $Relative -match '[:*?]' -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)' -or $Relative -match '[. ]([\\/]|$)'){throw "Unsafe relative path: $Relative"}
    $base=[IO.Path]::GetFullPath($Root).TrimEnd('\');$path=[IO.Path]::GetFullPath((Join-Path $base $Relative))
    if(-not $path.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Path escapes root: $Relative"}
    NoLink $path;return $path
}
function Target([string]$Relative){
    if($Relative -match '(^|[\\/])D18_Backups([\\/]|$)' -or
       $Relative -match '(^|[\\/])\.dlssnr-d18-install\.json$' -or $Relative -match '\.exe$'){throw "Reserved/game executable target: $Relative"}
    $path=Contained $script:game $Relative
    if(Test-Path -LiteralPath $path -PathType Container){throw "Expected a file: $path"};return $path
}
function ReadState([string]$Path){
    NoLink $Path;$state=Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    if($state.format -ne 'dlssnr-d18-install-state-v1'){throw "Unsupported state format: $($state.format). Use an uninstaller supporting that format."}
    if($state.PSObject.Properties.Name -contains 'game_dir'){
        $recorded=[IO.Path]::GetFullPath([string]$state.game_dir).TrimEnd('\')
        if(-not [string]::Equals($recorded,$script:game,[StringComparison]::OrdinalIgnoreCase)){throw "Record belongs to another game directory: $recorded"}
    };return $state
}
function ManagedPlan($State){
    $backup=Contained $script:game ([string]$State.backup_relative)
    if(-not $backup.StartsWith($script:game+'\D18_Backups\',[StringComparison]::OrdinalIgnoreCase)){throw 'Backup must be inside this game D18_Backups'}
    if(-not (Test-Path -LiteralPath $backup -PathType Container)){throw "Backup directory missing: $backup"}
    $seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase);$items=@()
    foreach($record in @($State.files)){
        $relative=[string]$record.target_relative;$path=Target $relative
        if(-not $seen.Add($path)){throw "Duplicate target: $relative"}
        if($record.existed -isnot [bool] -or [string]$record.installed_sha256 -notmatch '^[0-9a-fA-F]{64}$'){throw "Invalid record: $relative"}
        $restore=$null;$restoreHash=$null
        if($record.existed){
            $restore=Contained $backup ([string]$record.backup_relative)
            if(-not (Test-Path -LiteralPath $restore -PathType Leaf)){throw "Backup file missing: $restore"};$restoreHash=Hash $restore
        }
        $present=Test-Path -LiteralPath $path -PathType Leaf;$hash=if($present){Hash $path}else{$null}
        $items += [pscustomobject]@{Relative=$relative;Target=$path;Present=$present;BeforeHash=$hash;Restore=$restore;RestoreHash=$restoreHash;
            InstalledHash=[string]$record.installed_sha256;Action=$(if($record.existed){'restore'}else{'remove'})}
    }
    if(-not $items.Count){throw 'Record contains no managed files'}
    return [pscustomobject]@{Items=$items;Backup=$backup}
}
function GameStopped{
    $names=@(Get-ChildItem -LiteralPath $script:game -File -Filter '*.exe' | ForEach-Object {$_.BaseName})
    foreach($process in Get-Process){
        $path=$null;try{$path=$process.Path}catch{}
        if(($path -and $path.StartsWith($script:game+'\',[StringComparison]::OrdinalIgnoreCase)) -or
           (-not $path -and $process.ProcessName -in $names)){throw "Exit the game before uninstalling: $($process.ProcessName)"}
    }
}
function CatalogEntry($Map,[string]$Relative,[string]$Digest,[string]$Kind){
    $Relative=$Relative.Replace('/','\')
    if($Digest -notmatch '^[0-9a-fA-F]{64}$'){throw 'Invalid hash in uninstall catalog'}
    $null=Target $Relative
    if(-not $Map.ContainsKey($Relative)){$Map[$Relative]=[pscustomobject]@{Hashes=@();Kind=$Kind}}
    $Map[$Relative].Hashes += $Digest.ToUpperInvariant()
}
function ArchiveItem([string]$Relative){
    $path=Target $Relative;$digest=Hash $path
    return [pscustomobject]@{Relative=$Relative;Target=$path;Present=$true;BeforeHash=$digest;Restore=$null;RestoreHash=$null;InstalledHash=$digest;Action='archive'}
}
function ManualPlan{
    $catalog=@{};$catalogPath=Join-Path $PSScriptRoot 'uninstall-catalog.json'
    if(Test-Path -LiteralPath $catalogPath -PathType Leaf){
        $data=Get-Content -LiteralPath $catalogPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if($data.format -ne 'd18-uninstall-catalog-v1'){throw 'Unsupported uninstall catalog'}
        foreach($entry in $data.files){foreach($digest in $entry.sha256){CatalogEntry $catalog $entry.path $digest $entry.kind}}
    }
    # New packages extend exact-hash recognition without a version check in this script.
    $manifestPath=Join-Path $PSScriptRoot 'payload_manifest.json'
    if(Test-Path -LiteralPath $manifestPath -PathType Leaf){
        $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach($entry in $manifest.files){
            $relative=[string]$entry.path
            foreach($prefix in @('','_storage_\')){
                if($relative -eq 'OptiScaler.dll'){
                    foreach($proxy in @('dxgi.dll','d3d12.dll','winmm.dll','version.dll','dbghelp.dll')){CatalogEntry $catalog ($prefix+$proxy) $entry.sha256 'core'}
                }elseif($relative -eq 'D24Native.dll'){CatalogEntry $catalog ($prefix+$relative) $entry.sha256 'native'}
                elseif($relative -eq 'nvngx.dll_dlssnr.dll'){CatalogEntry $catalog ($prefix+$relative) $entry.sha256 'forwarder'}
                elseif($relative -match '^OptiScaler[\\/]'){CatalogEntry $catalog ($prefix+$relative) $entry.sha256 'dependency'}
            }
        }
    }
    $selected=@{};$anchors=@{}
    foreach($relative in @($catalog.Keys | Sort-Object)){
        $path=Target $relative;if(-not (Test-Path -LiteralPath $path -PathType Leaf)){continue}
        if((Hash $path) -in $catalog[$relative].Hashes){
            $selected[$relative]=ArchiveItem $relative
            if($catalog[$relative].Kind -in @('core','runtime','forwarder','native')){
                $prefix=if($relative -like '_storage_\*'){'_storage_\'}else{''};$anchors[$prefix]=$true
            }
        }else{Write-Host "Keep unrecognised/modified file: $relative" -ForegroundColor Yellow}
    }
    # Shared SDK dependencies alone are not evidence of D18 ownership.
    if(-not $anchors.Count){$selected=@{}}
    else{
        foreach($relative in @($selected.Keys)){
            $prefix=if($relative -like '_storage_\*'){'_storage_\'}else{''}
            if(-not $anchors.ContainsKey($prefix)){$selected.Remove($relative)}
        }
    }
    foreach($prefix in $anchors.Keys){
        foreach($name in @('OptiScaler.ini','OptiScaler.log','D18Diagnostics.ring','D24Native.log','D24VulkanNR.enabled')){
            $relative=$prefix+$name;if(Test-Path -LiteralPath (Target $relative) -PathType Leaf){$selected[$relative]=ArchiveItem $relative}
        }
    }
    foreach($relative in $ManualFiles){
        if(-not (Test-Path -LiteralPath (Target $relative) -PathType Leaf)){throw "Explicit manual file missing: $relative"}
        $selected[$relative]=ArchiveItem $relative
    };return @($selected.Values | Sort-Object Relative)
}
try{
    if($StateFile -and ($Manual -or $ManualFiles.Count)){throw 'Choose either -StateFile or manual cleanup parameters'}
    if(-not $GameDir){if($Yes -or $PlanOnly){throw 'Specify -GameDir'};$GameDir=Read-Host 'Game directory containing the D18 proxy'}
    $script:game=(Resolve-Path -LiteralPath $GameDir).Path.TrimEnd('\');NoLink $game
    $rootState=Join-Path $game '.dlssnr-d18-install.json';$statePath=$null;$backupRoot=$null
    if(Test-Path -LiteralPath $rootState -PathType Leaf){
        if($Manual -or $ManualFiles.Count){throw 'Managed record exists. Use managed uninstall instead of bypassing its recovery information.'}
        if($StateFile -and -not [string]::Equals([IO.Path]::GetFullPath($StateFile),$rootState,[StringComparison]::OrdinalIgnoreCase)){throw 'Root installation record takes precedence'}
        $statePath=$rootState
    }elseif($StateFile){
        $statePath=[IO.Path]::GetFullPath($StateFile)
        if(-not $statePath.StartsWith($game+'\D18_Backups\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($statePath) -ne 'install-state.json'){throw '-StateFile must be install-state.json inside this game D18_Backups'}
    }elseif(-not $Manual -and -not $ManualFiles.Count){
        $container=Join-Path $game 'D18_Backups';$candidates=@();$matches=@();NoLink $container
        if(Test-Path -LiteralPath $container -PathType Container){
            foreach($directory in Get-ChildItem -LiteralPath $container -Directory){
                NoLink $directory.FullName;$path=Join-Path $directory.FullName 'install-state.json'
                if(-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Test-Path -LiteralPath (Join-Path $directory.FullName 'uninstalled.json'))){continue}
                $candidates += $path
                try{
                    $candidate=ReadState $path;$validated=ManagedPlan $candidate
                    if(-not [string]::Equals($validated.Backup,$directory.FullName,[StringComparison]::OrdinalIgnoreCase)){continue}
                    if($candidate.PSObject.Properties.Name -contains 'proxy_name'){
                        $proxy=@($validated.Items | Where-Object {$_.Relative -eq $candidate.proxy_name -and $_.Present -and $_.BeforeHash -eq $_.InstalledHash})
                        if($proxy.Count -eq 1){$matches += $path}
                    }
                }catch{Write-Host "Backup not auto-selectable: $path ($($_.Exception.Message))" -ForegroundColor Yellow}
            }
        }
        if($matches.Count -eq 1){$statePath=$matches[0];Write-Host "Recovered installation record: $statePath"}
        elseif($candidates.Count){
            Write-Host 'Backup records (no unique verified active record):'
            for($i=0;$i -lt $candidates.Count;$i++){Write-Host "  $($i+1). $($candidates[$i])"}
            if($Yes -or $PlanOnly){throw 'Select an exact backup with -StateFile, or use -Manual for archive-only cleanup. No files changed.'}
            $choice=Read-Host 'Backup number, or M for archive-only cleanup (Enter cancels)';$index=0
            if($choice -ieq 'M'){$Manual=$true}
            elseif([int]::TryParse($choice,[ref]$index) -and $index -ge 1 -and $index -le $candidates.Count){$statePath=$candidates[$index-1]}
            else{throw 'Uninstall cancelled'}
        }
    }
    if($statePath){$state=ReadState $statePath;$managed=ManagedPlan $state;$plan=@($managed.Items);$backupRoot=$managed.Backup;$mode='managed-restore'}
    else{$plan=@(ManualPlan);$mode='manual-archive';Write-Host 'Archive recognised D18 files. Without an installation record, pre-install originals cannot be reconstructed.'}
    Write-Host "Game: $game`nMode: $mode"
    foreach($item in $plan){Write-Host ('  {0,-8} {1}' -f $item.Action,$item.Relative)}
    if(-not $plan.Count){Write-Host 'No recognised D18 files to remove. Unknown files were kept; use -ManualFiles only for files you have identified.';exit 0}
    if($PlanOnly){Write-Host "Preview only: $($plan.Count) files; no changes.";exit 0}
    GameStopped
    if(-not $Yes -and (Read-Host 'Continue with the listed operations? [y/N]') -notmatch '^(y|yes)$'){throw 'Uninstall cancelled'}
    $transaction=Contained $game ('D18_Backups\uninstall-recovery-'+(Get-Date -Format yyyyMMdd_HHmmss)+'-'+[guid]::NewGuid().ToString('N'))
    $snapshotRoot=Join-Path $transaction 'files';New-Item -ItemType Directory -Path $snapshotRoot -Force | Out-Null
    # Snapshot and verify every affected file before the first restore/remove operation.
    foreach($item in $plan){
        if((Test-Path -LiteralPath $item.Target -PathType Leaf) -ne $item.Present){throw "File changed after preflight: $($item.Relative)"}
        if($item.Present){
            if((Hash $item.Target) -ne $item.BeforeHash){throw "File changed after preflight: $($item.Relative)"}
            $snapshot=Contained $snapshotRoot $item.Relative;New-Item -ItemType Directory -Path (Split-Path -Parent $snapshot) -Force | Out-Null
            Copy-Item -LiteralPath $item.Target -Destination $snapshot
            if((Hash $snapshot) -ne $item.BeforeHash){throw 'Snapshot verification failed'}
        }
        if($item.Restore -and (Hash $item.Restore) -ne $item.RestoreHash){throw 'Original backup changed after preflight'}
    }
    $hadRoot=Test-Path -LiteralPath $rootState -PathType Leaf
    if($hadRoot){Copy-Item -LiteralPath $rootState -Destination (Join-Path $transaction 'root-state.json');if((Hash $rootState) -ne (Hash (Join-Path $transaction 'root-state.json'))){throw 'State snapshot verification failed'}}
    $previousMarker=$null
    if($backupRoot){
        $markerPath=Contained $backupRoot 'uninstalled.json'
        if(Test-Path -LiteralPath $markerPath -PathType Leaf){
            $previousMarker=Join-Path $transaction 'previous-uninstalled.json'
            Copy-Item -LiteralPath $markerPath -Destination $previousMarker
            if((Hash $markerPath) -ne (Hash $previousMarker)){throw 'Marker snapshot verification failed'}
        }
    }
    $journal=[ordered]@{format='d18-uninstall-recovery-v1';game_dir=$game;mode=$mode;source_state=$statePath;files=$plan;status='prepared'}
    $journal | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $transaction 'recovery.json') -Encoding UTF8
    GameStopped;$applied=@();$marker=$null
    try{
        foreach($item in $plan){
            $applied += $item
            if($item.Restore){
                New-Item -ItemType Directory -Path (Split-Path -Parent $item.Target) -Force | Out-Null
                Copy-Item -LiteralPath $item.Restore -Destination $item.Target -Force
                if((Hash $item.Target) -ne $item.RestoreHash){throw 'Restored file verification failed'}
            }elseif(Test-Path -LiteralPath $item.Target -PathType Leaf){Remove-Item -LiteralPath $item.Target -Force}
        }
        if($hadRoot){Remove-Item -LiteralPath $rootState -Force}
        if($backupRoot){$marker=Join-Path $backupRoot 'uninstalled.json';[pscustomobject]@{recovery=$transaction;time=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8}
        $journal.status='complete';$journal | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $transaction 'recovery.json') -Encoding UTF8
    }catch{
        $failure=$_.Exception.Message;$rollbackErrors=@()
        foreach($item in $applied){
            try{
                if($item.Present){Copy-Item -LiteralPath (Contained $snapshotRoot $item.Relative) -Destination $item.Target -Force;if((Hash $item.Target) -ne $item.BeforeHash){throw 'Rollback hash mismatch'}}
                elseif(Test-Path -LiteralPath $item.Target -PathType Leaf){Remove-Item -LiteralPath $item.Target -Force}
            }catch{$rollbackErrors += $_.Exception.Message}
        }
        try{if($hadRoot){Copy-Item -LiteralPath (Join-Path $transaction 'root-state.json') -Destination $rootState -Force};if($marker){if($previousMarker){Copy-Item -LiteralPath $previousMarker -Destination $marker -Force}elseif(Test-Path -LiteralPath $marker){Remove-Item -LiteralPath $marker -Force}}}catch{$rollbackErrors += $_.Exception.Message}
        throw "Uninstall failed: $failure. Recovery snapshot: $transaction. Rollback errors: $($rollbackErrors -join '; ')"
    }
    Write-Host "D18 uninstall completed ($mode). All pre-uninstall files retained at: $transaction" -ForegroundColor Green
    if($mode -eq 'manual-archive'){Write-Host 'Only recognised/explicitly selected files were archived. Unrecognised files were kept.'}
    exit 0
}catch{Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red;exit 1}
