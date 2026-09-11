#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$RequestPath,[Parameter(Mandatory=$true)][string]$ResultPath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'D18-Common.ps1')
. (Join-Path $PSScriptRoot 'D18-Dependencies.ps1')
# Requests omit unused optional fields; mandatory install arguments are checked by the installer.
Set-StrictMode -Off
$result=[ordered]@{schema='d18-gui-result-v1';success=$false;action='';code='operation_failed';message='';data=$null}
try {
 $r=Get-Content -LiteralPath $RequestPath -Raw -Encoding UTF8 | ConvertFrom-Json
 $result.action=$r.action
 $cache=if($r.cache){[string]$r.cache}else{Join-Path $env:LOCALAPPDATA 'D18\DownloadCache'}
 if($r.action -eq 'Discover'){
  . (Join-Path $PSScriptRoot 'D18-GameDiscovery.ps1')
  $result.data=@(Get-D18GameCandidates); $result.success=$true
 } elseif($r.action -eq 'Catalog') {
  $result.data=Get-D18DownloadCatalog $cache; $result.success=$true
 } elseif($r.action -eq 'InstallVC') {
  $null=New-Item -ItemType Directory -Path $cache -Force
  $path=Join-Path $cache 'vc_redist.x64.exe'
  Get-D18RemoteFile 'https://aka.ms/vc14/vc_redist.x64.exe' $path
  $sig=Get-AuthenticodeSignature -LiteralPath $path
  if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'Microsoft'){throw 'Microsoft runtime signature validation failed.'}
  $proc=Start-Process -FilePath $path -ArgumentList '/install','/passive','/norestart' -Verb RunAs -Wait -PassThru
  $result.success=$proc.ExitCode -in @(0,1638,3010); $result.data=@{reboot=($proc.ExitCode -eq 3010)}
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
  $cmd='$s=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("'+$data+'"))|ConvertFrom-Json;$p=@{};$s.args.PSObject.Properties|ForEach-Object{$p[$_.Name]=$_.Value};& $s.script @p;exit $LASTEXITCODE'
  $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($cmd))
  $log=Join-Path (Split-Path -Parent $ResultPath) 'backend.log'
  & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $encoded *> $log
  $result.success=($LASTEXITCODE -eq 0);$result.message=[IO.File]::ReadAllText($log)
  if(Test-Path -LiteralPath $innerResult){$inner=Get-Content -LiteralPath $innerResult -Raw|ConvertFrom-Json;$result.data=$inner.data;$result.data|Add-Member prepared_plan $planPath}
 }
} catch {$result.message=$_.Exception.Message}
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
