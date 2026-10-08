#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$RequestPath,[Parameter(Mandatory=$true)][string]$ResultPath,[switch]$Offline)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'D18-Common.ps1')
. (Join-Path $PSScriptRoot 'D18-Dependencies.ps1')
. (Join-Path $PSScriptRoot 'D18-REFramework.ps1')
Set-StrictMode -Off
$r=Get-Content -LiteralPath $RequestPath -Raw -Encoding UTF8|ConvertFrom-Json
$answer=@{success=$false;code='operation_failed';message='';data=$null}
try {
 $work=Split-Path -Parent $ResultPath
 if($r.action -eq 'ValidateNr') {
  $out=Join-Path $work 'nr-check.dll'
  try {$nr=New-D18PatchedRuntime $r.runtime $out (Join-Path $PSScriptRoot 'runtime_patch.json');$answer.data=$nr;$answer.success=$true}
  catch {$answer.data=@{Classification='CONFLICT'};$answer.message=$_.Exception.Message}
  finally {if(Test-Path -LiteralPath $out){Remove-Item -LiteralPath $out -Force}}
 } elseif($r.action -eq 'Meta') {
  $requestedGame=if($r.game){[string]$r.game}elseif($r.exe){Split-Path -Parent ([IO.Path]::GetFullPath([string]$r.exe))}else{''}
  $scope=Get-D18GameDirectoryScope $requestedGame
  $game=$scope.path
  if(-not $scope.allowed){
   # Metadata remains usable for uninstalling an older installation without an EXE.
   $managed=$false
   if($game -and (Test-Path -LiteralPath $game -PathType Container)){$managed=Test-Path -LiteralPath (Join-Path $game '.dlssnr-d18-install.json') -PathType Leaf}
   $answer.data=@{game=$game;exe=[string]$r.exe;scope=$scope;managed=$managed}
   $answer.success=$true
  } else {
  $selectedExe=[string]$r.exe
  if($selectedExe){
   . (Join-Path $PSScriptRoot 'D18-GameDiscovery.ps1')
   $selectedExe=[D18.Bootstrap]::Resolve($selectedExe)
   $game=Split-Path -Parent $selectedExe
  }
  $state=Join-Path $game '.dlssnr-d18-install.json'
  $profile=Get-D18ReProfile $game
  $api='None';$origin='guess';$proxy=(Get-D18ProxyRecommendation -Game $game -IsRE $profile.IsRE).Name
  if(Test-Path -LiteralPath $state){$old=Get-Content -LiteralPath $state -Raw -Encoding UTF8|ConvertFrom-Json;$api=$old.native_api;$proxy=$old.proxy_name;$origin='upgrade'}
  elseif((Test-Path (Join-Path $game 'nioh2.exe')) -or (Test-Path (Join-Path $game 'GRW.exe'))){$api='DX11';$origin='known'}
  elseif(Test-Path (Join-Path $game 'DOOMTheDarkAges.exe')){$api='Vulkan';$origin='known'}
  elseif($profile.IsRE -or @(Get-ChildItem -LiteralPath $game -Filter '*.exe'|Where-Object Name -match '^(Cyberpunk2077|Client-Win64-Shipping|FirstLight)\.exe$').Count){$origin='known'}
  elseif((Test-Path (Join-Path $game 'witcher3.exe')) -and $game -match '(?i)[\\/]x64_dx12$'){$origin='known'}
  $sr=Join-Path $game 'nvngx_dlss.dll';$fg=Join-Path $game 'streamline\nvngx_dlssg.dll'
  $gpu=@(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue|ForEach-Object {"$($_.Name) / $($_.DriverVersion)"})
  if(-not $gpu.Count){$gpu=@(Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Video\*\0000' -ErrorAction SilentlyContinue|Where-Object DriverDesc|ForEach-Object {"$($_.DriverDesc) / $($_.DriverVersion)"}|Select-Object -Unique)}
  $scope=Get-D18GameDirectoryScope $game
  $answer.data=@{game=$game;exe=$selectedExe;scope=$scope;api=$api;origin=$origin;proxy=$proxy;managed=(Test-Path $state);re_engine=$profile.IsRE;system=(Get-D18SystemDependencies $api);gpu=$gpu;
   sr_version=$(if(Test-Path $sr){[Diagnostics.FileVersionInfo]::GetVersionInfo($sr).FileVersion}else{''});fg_version=$(if(Test-Path $fg){[Diagnostics.FileVersionInfo]::GetVersionInfo($fg).FileVersion}else{''})}
  $answer.success=$true
  }
 } else {
  if($Offline -and ($r.action -in @('Catalog','InstallVC','DownloadOptional') -or $r.srMode -eq 'Download' -or $r.fgMode -eq 'Download' -or $r.ref -in @('Recommended','Latest') -or ($r.action -eq 'PrepareDependencies' -and ($r.includeSr -or $r.includeFg -or $r.installVc)))){throw 'Offline mode: this selection requires a download or system installation. Use local files.'}
  if($r.action -in @('Check','Install')) {
   $requestedGame=if($r.game){[string]$r.game}elseif($r.exe){Split-Path -Parent ([IO.Path]::GetFullPath([string]$r.exe))}else{''}
   $null=Assert-D18GameDirectoryScope $requestedGame
   . (Join-Path $PSScriptRoot 'D18-GameDiscovery.ps1')
   $exe=[D18.Bootstrap]::Resolve([string]$r.exe)
   if([IO.Path]::GetExtension($exe) -ine '.exe' -or -not(Test-Path -LiteralPath $exe -PathType Leaf)){throw 'Choose the actual game executable.'}
   $game=Split-Path -Parent $exe
   $null=Assert-D18GameDirectoryScope $game
   $cursor=$game
   while($cursor){
    if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Select the real game directory; linked deployment path: $cursor"}
    $cursor=Split-Path -Parent $cursor
   }
   if($r.game -and [IO.Path]::GetFullPath($r.game).TrimEnd('\') -ine $game){throw 'Selected folder differs from the game executable folder.'}
   if($game -ieq $PSScriptRoot){throw 'Choose a game directory, not the installer directory.'}
   $r.exe=$exe;$r.game=$game
   if($Offline -and $r.ref -eq 'Auto' -and (Get-D18ReProfile $game -ForceRE:([bool]$r.reEngine)).IsRE -and
      -not(Test-Path -LiteralPath (Join-Path $game 'dinput8.dll')) -and -not $r.refPath){throw 'Offline mode: REFramework is missing. Select a local file or Manual / prepare later.'}
   if($r.apiOrigin -eq 'guess' -and -not $r.apiConfirmed){throw 'Confirm the guessed graphics API.'}
   if($r.action -eq 'Install') {
    $nrOut=Join-Path $work 'nr-install-check.dll'
    try {$nr=New-D18PatchedRuntime $r.runtime $nrOut (Join-Path $PSScriptRoot 'runtime_patch.json')}
    finally {if(Test-Path -LiteralPath $nrOut){Remove-Item -LiteralPath $nrOut -Force}}
    if($nr.Classification -eq 'UNVERIFIED_COMPATIBLE' -and -not $r.ackNr){throw 'Confirm using an unverified compatible NR file.'}
   }
   if($r.optional) {
    if(-not $r.optionalPath){throw 'Choose the local optional components ZIP; the release URL is not configured yet.'}
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $definition=Get-Content (Join-Path $PSScriptRoot 'optional-components.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $destination=Join-Path $work 'optional-backend';$null=New-Item -ItemType Directory -Path $destination
    Get-ChildItem -LiteralPath $PSScriptRoot -Force|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $destination -Recurse}
    $z=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath([string]$r.optionalPath))
    try {
     # Extract only pinned SDK members. Traversal, duplicate paths and unknown executable entries are rejected.
     $allowed=@($definition.files|ForEach-Object {'payload/'+$_.path.Replace('\','/')})+@('optional-manifest.json')
     $seen=@{}
     foreach($e in $z.Entries){
      $n=$e.FullName.Replace('\','/')
      if($seen.ContainsKey($n) -or $n -match '(^/|(^|/)\.\.(/|$)|:)'){throw 'Unsafe optional ZIP entry.'};$seen[$n]=$true
      if($n -notin $allowed -and -not $n.StartsWith('Licenses/')){throw "Unexpected optional ZIP entry: $n"}
     }
     foreach($f in $definition.files) {
      $n='payload/'+$f.path.Replace('\','/');$entries=@($z.Entries|Where-Object {$_.FullName.Replace('\','/') -ceq $n})
      if($entries.Count -ne 1 -or $entries[0].Length -ne $f.size){throw "Missing/invalid SDK file: $n"}
      $target=Join-Path $destination $n;$null=New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force
      [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0],$target,$false)
      if((Get-D18Sha256 $target) -ine $f.sha256){throw "Optional SDK hash mismatch: $n"}
     }
    } finally {$z.Dispose()}
    $m=Get-Content (Join-Path $destination 'payload_manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $m.files=@($m.files)+@($definition.files)
    $m|ConvertTo-Json -Depth 15|Set-Content (Join-Path $destination 'payload_manifest.json') -Encoding UTF8
    $r|Add-Member backendRoot $destination -Force
   }
  }
  $r|ConvertTo-Json -Depth 24|Set-Content -LiteralPath $RequestPath -Encoding UTF8
  $invoke=[D18.SetupHost]::Run((Join-Path $PSScriptRoot 'D18-GuiWorker.ps1'),@{RequestPath=$RequestPath;ResultPath=$ResultPath;InProcess=$true})
  if(Test-Path -LiteralPath $ResultPath){exit $invoke.ExitCode}
  throw $invoke.Log
 }
 if($answer.success){$answer.code='done'}
} catch {
 $answer.message=$_.Exception.Message
 if($_.Exception.Data.Contains('D18Scope')){$answer.code='directory_scope';$answer.data=@{scope=$_.Exception.Data['D18Scope']}}
}
$answer|ConvertTo-Json -Depth 24|Set-Content -LiteralPath $ResultPath -Encoding UTF8
if($answer.success){exit 0}else{exit 1}
