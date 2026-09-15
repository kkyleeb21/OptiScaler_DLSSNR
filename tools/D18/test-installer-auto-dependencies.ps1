#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$Installer,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
. (Join-Path $Installer 'D18-Common.ps1')
. (Join-Path $Installer 'D18-Dependencies.ps1')
. (Join-Path $Installer 'D18-REFramework.ps1')
. (Join-Path $Installer 'D18-AutoDependencies.ps1')
Set-StrictMode -Off
$Output=[IO.Path]::GetFullPath($Output)
$root=Join-Path $Output ([guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root -Force
$cache=Join-Path $root 'cache';$null=New-Item -ItemType Directory -Path $cache
$bundle=Join-Path $root 'bundle';$null=New-Item -ItemType Directory -Path $bundle
$names=@('nvngx_dlss.dll','sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')
foreach($name in $names){[IO.File]::WriteAllText((Join-Path $bundle $name),('fixture '+$name))}
# Dependency network/OS and NR patch operations are substituted; filesystem plans and guards are real.
$script:fetches=0;$script:networkFail=$false;$script:nrFail=$false;$script:vcMissing=$false;$script:vcFail=$false;$script:vcCalls=0
function Assert-D18DependencyDll($Path,$ExpectedName){if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){throw 'Missing DLL'};if([IO.File]::ReadAllText($Path) -eq 'invalid'){throw 'Invalid DLL'}}
function Get-D18AutomaticDll($Kind,$Cache){$script:fetches++;if($script:networkFail){throw 'Network unavailable'};return Join-Path $bundle $(if($Kind -eq 'dlss'){'nvngx_dlss.dll'}else{'nvngx_dlssg.dll'})}
function Get-D18StreamlineBundle($Cache){$script:fetches++;if($script:networkFail){throw 'Network unavailable'};return $bundle}
function New-D18PatchedRuntime($SourcePath,$OutputPath){if($script:nrFail){throw 'Patch mismatch'};Copy-Item -LiteralPath $SourcePath -Destination $OutputPath}
function Test-D18Dx11Runtime($RuntimePath,$CheckerPath){return @{verified=$true}}
function Get-D18ReProfile($Game,[switch]$ForceRE){return @{IsRE=[bool]$ForceRE;Tested=$true;Catalog=@{tested=@{dll_sha256='matched'}}}}
function Get-D18RefDownload($Mode,$Profile,$Stage){$path=Join-Path $Stage 'dinput8.dll';[IO.File]::WriteAllText($path,'fixture REF');return $path}
function Get-D18SystemDependencies($Api){return @{vc_missing=@($(if($script:vcMissing){'MSVCP140.dll'}));graphics_missing=@()}}
function Install-D18VcRuntime($Cache){$script:vcCalls++;if($script:vcFail){throw 'UAC cancelled'};$script:vcMissing=$false;return @{reboot=$false;verified=$true}}
$script:checks=New-Object System.Collections.Generic.List[string]
function Check($condition,$name){if(-not $condition){throw "FAILED: $name"};$script:checks.Add($name)}
function Request($name){
 $game=Join-Path $root $name;$null=New-Item -ItemType Directory -Path $game
 $nr=Join-Path $root ($name+'-nr.dll');[IO.File]::WriteAllText($nr,'NR fixture')
 return @{game=$game;api='DX11';runtime=$nr;includeSr=$true;includeFg=$true;installVc=$true;srMode='Keep';fgMode='Keep';ref='Auto'}
}
$r=Request 'missing'
$a=Invoke-D18DependencyPreparation $r $cache $root
Check ($script:fetches -eq 0 -and -not $a.plan) 'audit performs no downloads or plan writes'
Check (@($a.rows|Where-Object status -eq 'missing').Count -eq 2) 'audit finds absent SR and FG'
$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
$plan=Get-Content $a.plan -Raw|ConvertFrom-Json
Check ($a.ready -and $plan.files.Count -eq 7) 'prepares complete SR and FG plan'
Check (@(Get-ChildItem -LiteralPath $r.game -Force).Count -eq 0) 'preparation does not write game files'
Check (@($plan.files|Where-Object {-not $_.expected_absent}).Count -eq 0) 'all automatic targets require absence'
$items=New-Object System.Collections.Generic.List[object]
Add-D18DependencyItems -PlanPath $a.plan -Items $items -Game $r.game -Stage $cache
Check ($items.Count -eq 7) 'prepared files enter normal managed transaction'
Copy-Item (Join-Path $bundle 'nvngx_dlss.dll') (Join-Path $r.game 'nvngx_dlss.dll')
$caught=$false;try{Add-D18DependencyItems -PlanPath $a.plan -Items $items -Game $r.game -Stage $cache}catch{$caught=$_.Exception.Message -match 'now exists'}
Check $caught 'late appearing target blocks overwrite'
$null=New-Item -ItemType Directory -Path (Join-Path $r.game 'streamline')
foreach($name in $names|Where-Object {$_ -ne 'nvngx_dlss.dll'}){Copy-Item (Join-Path $bundle $name) (Join-Path $r.game ('streamline\'+$name))}
$script:fetches=0;$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check ($a.ready -and $script:fetches -eq 0 -and @((Get-Content $a.plan -Raw|ConvertFrom-Json).files).Count -eq 0) 'complete existing bundle retained without download'
$r=Request 'partial';$null=New-Item -ItemType Directory -Path (Join-Path $r.game 'streamline');Copy-Item (Join-Path $bundle 'sl.common.dll') (Join-Path $r.game 'streamline\sl.common.dll')
$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check ($a.ready -and @((Get-Content $a.plan -Raw|ConvertFrom-Json).files).Count -eq 6) 'matching partial bundle fills only missing files'
[IO.File]::WriteAllText((Join-Path $r.game 'streamline\sl.common.dll'),'another version')
$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check (-not $a.ready -and ($a.rows|Where-Object id -eq 'FG').status -eq 'error' -and @((Get-Content $a.plan -Raw|ConvertFrom-Json).files|Where-Object target -like 'streamline*').Count -eq 0) 'different partial bundle is never mixed'
$r=Request 'offline';$script:networkFail=$true;$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check (-not $a.ready -and @($a.rows|Where-Object status -eq 'error').Count -eq 2) 'network failure reports per dependency without false success'
$script:networkFail=$false;$script:nrFail=$true;$r.includeSr=$false;$r.includeFg=$false;$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check (-not $a.ready -and -not $a.runtime -and ($a.rows|Where-Object id -eq 'NR').status -eq 'error') 'NR mismatch is not marked ready'
$script:nrFail=$false;$r.runtime=Join-Path $root 'absent-nr.dll';$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check (($a.rows|Where-Object id -eq 'NR').status -eq 'manual') 'missing NR gives manual action'
$r=Request 'manual';$r.srMode='Local';$r.fgMode='Download';$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check (@($a.rows|Where-Object status -eq 'selected').Count -eq 2) 'explicit sources are not overridden'
$script:vcMissing=$true;$script:vcFail=$true;$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check (-not $a.ready -and ($a.rows|Where-Object id -eq 'VC++').status -eq 'error') 'UAC cancellation is an actionable failure'
$script:vcFail=$false;$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check ($a.ready -and $script:vcCalls -eq 2) 'missing VC preparation and retry'
$r.reEngine=$true;$a=Invoke-D18DependencyPreparation $r $cache $root -Prepare
Check ($a.ref_path -and (Test-Path $a.ref_path) -and -not(Test-Path (Join-Path $r.game 'dinput8.dll'))) 'matched REF stages outside the game'
$caught=$false;try{Invoke-D18DependencyPreparation $r (Join-Path $r.game 'cache') $root -Prepare}catch{$caught=$_.Exception.Message -match 'outside'}
Check $caught 'cache inside game is rejected'
$result=@{pass=$true;scope='offline fixture; network, VC/UAC and NR validation substituted';checks=@($script:checks.ToArray());root=$root}
$result|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Output 'result.json') -Encoding UTF8
$result|ConvertTo-Json -Depth 5
