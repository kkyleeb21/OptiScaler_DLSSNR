#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$RequestPath,[Parameter(Mandatory=$true)][string]$ResultPath)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$session=$null
. (Join-Path $PSScriptRoot 'D18-Common.ps1')
. (Join-Path $PSScriptRoot 'D18-Dependencies.ps1')
. (Join-Path $PSScriptRoot 'D18-REFramework.ps1')
. (Join-Path $PSScriptRoot 'D18-AutoDependencies.ps1')
. (Join-Path $PSScriptRoot 'D18-Cache.ps1')
# Requests omit unused optional fields; mandatory install arguments are checked by the installer.
Set-StrictMode -Off
$result=[ordered]@{schema='d18-gui-result-v1';success=$false;action='';code='operation_failed';message='';data=$null}
try {
 $r=Get-Content -LiteralPath $RequestPath -Raw -Encoding UTF8 | ConvertFrom-Json
 $result.action=$r.action
 $cache=if($r.cache){[string]$r.cache}else{Join-Path $env:LOCALAPPDATA 'D18\DownloadCache'}
 if($r.cacheSession -and $r.action -ne 'Discover'){
  $gameForCache=if($r.game){[string]$r.game}elseif($r.exe){Split-Path -Parent ([IO.Path]::GetFullPath($r.exe))}else{''}
  $session=Open-D18CacheSession -Base $cache -Id $r.cacheSession -Game $gameForCache
  $cache=$session.root
  if($r.originalNr){Protect-D18OriginalNr $session $r.originalNr}
  elseif($r.action -eq "PrepareDependencies" -and $r.runtime){Protect-D18OriginalNr $session $r.runtime}
 }
 if($r.action -eq 'CleanupCache'){
  if(-not $session){throw 'A managed cache session is required for cleanup.'}
  $result.data=Clear-D18InstalledCache -Session $session -Ticket $r.cleanupTicket -Game $gameForCache -Receipt (Join-Path (Split-Path -Parent $ResultPath) 'cleanup-receipt.json')
  $result.success=$true
 } elseif($r.action -eq 'Discover'){
  . (Join-Path $PSScriptRoot 'D18-GameDiscovery.ps1')
  $result.data=@(Get-D18GameCandidates); $result.success=$true
 } elseif($r.action -eq 'Catalog') {
  $result.data=Get-D18DownloadCatalog $cache; $result.success=$true
 } elseif($r.action -eq 'InstallVC') {
  $result.data=Install-D18VcRuntime $cache; $result.success=$true
 } elseif($r.action -in @('AuditDependencies','PrepareDependencies')) {
  $result.data=Invoke-D18DependencyPreparation -Request $r -Cache $cache -ResultDirectory (Split-Path -Parent $ResultPath) -Prepare:($r.action -eq 'PrepareDependencies')
  $result.success=$true
 } else {
  if($r.action -notin @('Check','Install','Uninstall')){throw 'Invalid action.'}
  if(-not(Test-Path -LiteralPath $r.exe -PathType Leaf) -or [IO.Path]::GetExtension($r.exe) -ine '.exe'){throw '[GAME_EXE]'}
  $game=Split-Path -Parent ([IO.Path]::GetFullPath($r.exe))
  $planPath=[string]$r.preparedPlan
  if($r.action -eq 'Check'){
   $files=@()
   if($r.srMode -eq 'Download'){$sr=Get-D18SwapperDll $r.srEntry 'nvngx_dlss.dll' $cache}
   elseif($r.srMode -eq 'Local'){$sr=[string]$r.srPath}else{$sr=$null}
   if($sr){Assert-D18DependencyDll $sr 'nvngx_dlss.dll';$files+=@{source=$sr;target='nvngx_dlss.dll';sha256=(Get-D18Sha256 $sr)}}
   if($r.fgMode -in @('Download','Local')){
    if($r.fgMode -eq 'Download'){$fgdir=Get-D18StreamlineBundle $cache; $fg=Get-D18SwapperDll $r.fgEntry 'nvngx_dlssg.dll' $cache}
    else{$fgdir=[string]$r.fgPath; $fg=Join-Path $fgdir 'nvngx_dlssg.dll'}
    foreach($name in @('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')){
     $source=if($name -eq 'nvngx_dlssg.dll'){$fg}else{Join-Path $fgdir $name}
     Assert-D18DependencyDll $source $name
     $files+=@{source=$source;target=('streamline\'+$name);sha256=(Get-D18Sha256 $source)}
    }
   }
   if($r.autoPlan){
    $auto=Get-Content -LiteralPath $r.autoPlan -Raw -Encoding UTF8|ConvertFrom-Json
    if($auto.schema -ne 'd18-auto-dependencies-v1' -or $auto.game.TrimEnd('\') -ine $game.TrimEnd('\') -or $auto.api -ne $r.api){throw 'Prepared dependency plan belongs to a different game/API. Check again.'}
    foreach($file in @($auto.files)){
     $manual=($file.target -eq 'nvngx_dlss.dll' -and $r.srMode -in @('Download','Local')) -or ($file.target -like 'streamline\*' -and $r.fgMode -in @('Download','Local'))
     if(-not $manual){$files+=$file}
    }
   }
   $planPath=Join-Path (Split-Path -Parent $ResultPath) 'dependencies.json'
   @{files=@($files)} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $planPath -Encoding UTF8
  }
  $innerResult=Join-Path (Split-Path -Parent $ResultPath) 'installer-result.json'
  $refPath=[string]$r.refPath
  if($r.action -eq 'Install' -and $r.preparedRef){
   if((Get-D18Sha256 $r.preparedRef.path) -ine $r.preparedRef.sha256){throw 'Prepared REFramework file changed.'}
   $refPath=[string]$r.preparedRef.path
  }
  if($r.action -ne 'Uninstall'){
   $spec=@{script=(Join-Path $PSScriptRoot 'Install-D18.ps1');args=@{
    GameDir=$game;RuntimePath=[string]$r.runtime;NativeApi=[string]$r.api;ProxyName=[string]$r.proxy;REFramework=[string]$r.ref
    Yes=$true;CheckOnly=($r.action -eq 'Check');ResultPath=$innerResult;DependencyPlanPath=$planPath
    AcknowledgeAntiCheatRisk=[bool]$r.ack;REEngine=[bool]$r.reEngine;REFrameworkPath=$refPath;ConfirmExistingREFramework=[bool]$r.refConfirm
   }}
  } else {$spec=@{script=(Join-Path $PSScriptRoot 'Uninstall-D18.ps1');args=@{GameDir=$game;Yes=$true}}}
  $data=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($spec|ConvertTo-Json -Depth 8 -Compress)))
  $cmd='$ProgressPreference="SilentlyContinue";$s=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("'+$data+'"))|ConvertFrom-Json;$p=@{};$s.args.PSObject.Properties|ForEach-Object{$p[$_.Name]=$_.Value};& $s.script @p;exit $LASTEXITCODE'
  $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($cmd))
  $log=Join-Path (Split-Path -Parent $ResultPath) 'backend.log'
  & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $encoded *> $log
  $result.success=($LASTEXITCODE -eq 0);$result.message=[IO.File]::ReadAllText($log)
  if(Test-Path -LiteralPath $innerResult){$inner=Get-Content -LiteralPath $innerResult -Raw|ConvertFrom-Json;$result.data=$inner.data;$result.data|Add-Member prepared_plan $planPath}
 }
 if($r.action -eq 'Install' -and $result.success){$result.data|Add-Member cleanup_ticket '' -Force}
 if($r.action -eq 'Install' -and $result.success -and $session){
  try {
   Save-D18CacheInventory $session
   $ticket=New-D18CacheInstallTicket -Session $session -Game $game -Path (Join-Path (Split-Path -Parent $ResultPath) 'cache-install-ticket.json')
   $result.data|Add-Member cleanup_ticket $ticket -Force
   if($r.cleanupCache){
    $clean=Clear-D18InstalledCache -Session $session -Ticket $ticket -Game $game -Receipt (Join-Path (Split-Path -Parent $ResultPath) 'cleanup-receipt.json')
    $result.data|Add-Member cache_cleanup $clean -Force
   }
  } catch {$result.data|Add-Member cache_cleanup @{status='deferred';reason=$_.Exception.Message} -Force}
 }
} catch {$result.message=$_.Exception.Message}
finally {
 if($session){
  try{Save-D18CacheInventory $session}catch{$result.message+="`nCache inventory retained with error: "+$_.Exception.Message}
  $session.lock.Dispose()
 }
}
if($result.success){$result.code='done'}else{
 $result.code=switch -Regex ($result.message){
  'DX11_LAYOUT_CONFLICT'{'layout_conflict';break}
  'Close the game'{'game_running';break}
  'GAME_EXE'{'game_exe';break}
  'anti-cheat'{'anti_cheat';break}
  'REFramework|dinput8'{'reframework';break}
  'Access.*denied|拒绝访问'{'access_denied';break}
  'Runtime|patch|DLL type'{'runtime_conflict';break}
  default{'operation_failed'}
 }
}
$result|ConvertTo-Json -Depth 24|Set-Content -LiteralPath $ResultPath -Encoding UTF8
if($result.success){exit 0}else{exit 1}
