#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$Installer,[Parameter(Mandatory=$true)][string]$Fixture,
      [Parameter(Mandatory=$true)][string]$Cache,[Parameter(Mandatory=$true)][string]$Runtime,
      [Parameter(Mandatory=$true)][string]$HostExe)
$ErrorActionPreference='Stop'
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules;C:\Program Files\WindowsPowerShell\Modules"
$Fixture=[IO.Path]::GetFullPath($Fixture).TrimEnd('\')
if(-not $Fixture.StartsWith('E:\DLSSNR\reports\',[StringComparison]::OrdinalIgnoreCase)){throw 'Test fixture must be under the project reports folder.'}
if(Test-Path -LiteralPath $Fixture){throw 'Use a fresh workflow fixture.'}
$game=Join-Path $Fixture 'game';$null=New-Item -ItemType Directory -Path $game -Force
$exe=Join-Path $game 'GRW.exe';Copy-Item -LiteralPath $HostExe -Destination $exe
$ini=Join-Path $game 'OptiScaler.ini';Copy-Item -LiteralPath (Join-Path $Installer 'payload\OptiScaler.ini.d18') -Destination $ini
Add-Content -LiteralPath $ini -Value "`r`n[PreservationFixture]`r`nSentinel=keep-this-value"
[IO.File]::WriteAllText((Join-Path $game 'unrelated.txt'),'preserve unrelated files')
$before=@{};Get-ChildItem -LiteralPath $game -File|ForEach-Object {$before[$_.Name]=(Get-FileHash -LiteralPath $_.FullName).Hash}
function Worker($step,$request,[bool]$Expected=$true){
 $dir=Join-Path $Fixture $step;$null=New-Item -ItemType Directory -Path $dir
 $req=Join-Path $dir 'request.json';$result=Join-Path $dir 'result.json'
 $request|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $req -Encoding UTF8
 & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $Installer 'D18-GuiWorker.ps1') -RequestPath $req -ResultPath $result *> (Join-Path $dir 'worker.log')
 $exit=$LASTEXITCODE;$r=Get-Content -LiteralPath $result -Raw -Encoding UTF8|ConvertFrom-Json
 if($r.success -ne $Expected -or (($exit -eq 0) -ne $Expected)){throw "$step failed unexpectedly: $($r.message)"}
 Write-Host "$step : expected success=$Expected"
 return $r
}
$prepared=Worker 'prepare' @{action='PrepareDependencies';game=$game;api='DX11';runtime=$Runtime;includeSr=$true;includeFg=$true;installVc=$false;srMode='Keep';fgMode='Keep';ref='Auto';cache=$Cache}
if(-not $prepared.data.ready){throw ($prepared.data.rows|ConvertTo-Json -Depth 4)}
foreach($name in $before.Keys){if((Get-FileHash (Join-Path $game $name)).Hash -ne $before[$name]){throw 'Preparation changed fixture files.'}}
if(@(Get-ChildItem -LiteralPath $game -File).Count -ne $before.Count){throw 'Preparation deployed files.'}
$request=@{action='Check';exe=$exe;api='DX11';runtime=$prepared.data.runtime;proxy='dxgi.dll';ref='Auto';ack=$true;srMode='Keep';fgMode='Keep';autoPlan=$prepared.data.plan;cache=$Cache}
$check=Worker 'check' $request
$request.action='Install';$request.preparedPlan=$check.data.prepared_plan
$null=Worker 'install' $request
function VerifyInstalled {
 $state=Get-Content -LiteralPath (Join-Path $game '.dlssnr-d18-install.json') -Raw|ConvertFrom-Json
 foreach($file in $state.files){if((Get-FileHash -LiteralPath (Join-Path $game $file.target_relative)).Hash -ine $file.installed_sha256){throw "Installed hash mismatch: $($file.target_relative)"}}
 if(-not(Test-Path (Join-Path $game 'D18WildlandsSR.enabled'))){throw 'Production native SR marker missing.'}
 if(-not([IO.File]::ReadAllText($ini).Contains('Sentinel=keep-this-value'))){throw 'User config value lost.'}
 return $state
}
$state=VerifyInstalled
# Owned optional files are retained by the shared upgrade transaction with Keep mode.
$request.action='Check';$request.autoPlan='';$request.preparedPlan=''
$check=Worker 'upgrade-check' $request
$request.action='Install';$request.preparedPlan=$check.data.prepared_plan
$null=Worker 'upgrade' $request
$state=VerifyInstalled
$null=Worker 'uninstall' @{action='Uninstall';exe=$exe}
if(Test-Path (Join-Path $game '.dlssnr-d18-install.json')){throw 'Managed state remains after uninstall.'}
foreach($name in $before.Keys){if((Get-FileHash -LiteralPath (Join-Path $game $name)).Hash -ne $before[$name]){throw "Original file not restored: $name"}}
foreach($file in $state.files){if($file.target_relative -notin $before.Keys -and (Test-Path -LiteralPath (Join-Path $game $file.target_relative))){throw "New payload remains: $($file.target_relative)"}}
# A stale auto plan for another game must be rejected before any mutation.
$bad=Get-Content -LiteralPath $prepared.data.plan -Raw|ConvertFrom-Json;$bad.game=Join-Path $Fixture 'other-game'
$badPath=Join-Path $Fixture 'wrong-game-plan.json';$bad|ConvertTo-Json -Depth 10|Set-Content $badPath -Encoding UTF8
$request.action='Check';$request.autoPlan=$badPath;$request.preparedPlan=''
$null=Worker 'wrong-game-rejected' $request $false
$bad=Get-Content -LiteralPath $prepared.data.plan -Raw|ConvertFrom-Json;$bad.files[0].sha256=('0'*64)
$badPath=Join-Path $Fixture 'wrong-hash-plan.json';$bad|ConvertTo-Json -Depth 10|Set-Content $badPath -Encoding UTF8
$request.autoPlan=$badPath;$null=Worker 'wrong-hash-rejected' $request $false
@{pass=$true;scope='real PS5.1 worker; real downloaded and signed SR/FG, real NR layout; isolated fixture only';prepare=$true;check=$true;install=$true;upgrade=$true;uninstall_restore=$true;all_installed_hashes=$true;stale_plan_rejected=$true;changed_source_rejected=$true;game=$game}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Fixture 'result.json') -Encoding UTF8
Write-Host 'PASS: isolated prepare/check/install/upgrade/uninstall and failure guards.'
