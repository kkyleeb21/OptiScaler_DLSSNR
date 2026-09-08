param([Parameter(Mandatory=$true)][string]$PackageRoot,[Parameter(Mandatory=$true)][string]$Runtime,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$package=(Resolve-Path -LiteralPath $PackageRoot).Path
. (Join-Path $package 'D18-Common.ps1')
$results=New-Object System.Collections.Generic.List[object]
function Invoke-Install([string]$Game,[string]$Api,[bool]$Supply=$false,[string]$Script='') {
 if(-not $Script){$Script=Join-Path $package 'Install-D18.ps1'}
 $args=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$Script,'-GameDir',$Game,'-ProxyName','dxgi.dll','-Yes')
 if($Api){$args+=@('-NativeApi',$Api)}
 if($Supply){$args+=@('-RuntimePath',$Runtime)}
 $log=Join-Path $OutputRoot ([guid]::NewGuid().ToString('N')+'.log')
 & powershell.exe @args *> $log
 return [pscustomobject]@{code=$LASTEXITCODE;log=$log}
}
function Assert-True([bool]$Value,[string]$Message){if(-not $Value){throw $Message}}
function Snapshot([string]$Game){
 $s=Get-Content -LiteralPath (Join-Path $Game '.dlssnr-d18-install.json') -Raw | ConvertFrom-Json
 $h=@{};foreach($name in (@($s.files | ForEach-Object target_relative)+'.dlssnr-d18-install.json')){$h[$name]=Get-D18Sha256 (Join-Path $Game $name)};return $h
}
foreach($api in @('DX11','Vulkan')){
 $game=Join-Path $OutputRoot $api;New-Item -ItemType Directory -Path $game | Out-Null
 Set-Content (Join-Path $game 'Fixture.exe') 'Not executable, never launched'
 $original="[Upscalers]`r`nDx11Upscaler=fsr31`r`nVulkanUpscaler=fsr31`r`n[DLSS]`r`nEnabled=false`r`n[Menu]`r`nD18Language=1`r`nKey=36`r`n[DlssNr]`r`nJitterCorrection=false`r`nToggleKey=34`r`n[UserCustom]`r`nKeepMe=42`r`n"
 Set-Content -LiteralPath (Join-Path $game 'OptiScaler.ini') -Value $original -NoNewline
 $run=Invoke-Install $game $api $true;Assert-True ($run.code -eq 0) "Install failed: $($run.log)"
 $text=[IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini'))
 $key=if($api -eq 'DX11'){'Dx11Upscaler'}else{'VulkanUpscaler'}
 $expected=Set-D18IniValue -Text $original -Section Upscalers -Key $key -Value dlss
 $expected=Set-D18IniValue -Text $expected -Section DLSS -Key Enabled -Value true
 Assert-True ($text -ceq $expected) 'Migration changed unrelated settings'
 $before=Snapshot $game
 $run=Invoke-Install $game '';Assert-True ($run.code -eq 0) "Runtime discovery upgrade failed: $($run.log)"
 Assert-True ((Get-D18Sha256 (Join-Path $game 'OptiScaler.ini')) -eq $before['OptiScaler.ini']) 'Same-API upgrade changed configuration'
 $other=if($api -eq 'DX11'){'Vulkan'}else{'DX11'}
 $run=Invoke-Install $game $other;Assert-True ($run.code -eq 0) "API switch failed: $($run.log)"
 Assert-True ((Test-Path (Join-Path $game 'D24Native.dll')) -eq ($other -eq 'DX11')) 'Old API addon not removed'
 Assert-True ((Test-Path (Join-Path $game 'D24VulkanNR.enabled')) -eq ($other -eq 'Vulkan')) 'Old API marker not removed'
 $results.Add(@{api=$api;migration='pass';unrelated_settings='pass';upgrade_without_source='pass';switch_api='pass'})
}
# Inject deterministic faults only into a private test copy; no test switches in released installer.
$faultPackage=Join-Path $OutputRoot 'fault-package';Copy-Item -LiteralPath $package -Destination $faultPackage -Recurse
$faultScript=Join-Path $faultPackage 'Install-D18.ps1'
$baseline=[IO.File]::ReadAllText($faultScript)
$game=Join-Path $OutputRoot 'DX11'
foreach($stage in @('after-uninstall','after-copy')){
 $needle=if($stage -eq 'after-uninstall'){'$existingInstallRemoved = $true'}else{'$installedHash = Get-D18Sha256 -LiteralPath $target'}
 Assert-True ($baseline.Contains($needle)) 'Fault injection marker missing'
 $patched=$baseline.Replace($needle,($needle+"`r`nthrow 'Controlled fixture failure: $stage'"))
 [IO.File]::WriteAllText($faultScript,$patched,[Text.UTF8Encoding]::new($false))
 $before=Snapshot $game
 $run=Invoke-Install $game '' $false $faultScript
 Assert-True ($run.code -ne 0) 'Fault was not triggered'
 foreach($name in $before.Keys){Assert-True ((Get-D18Sha256 (Join-Path $game $name)) -eq $before[$name]) "Old deployment not restored: $name"}
 Assert-True ((Get-Content $run.log -Raw).Contains('Previous D18 installation and settings restored and hash-verified.')) 'Missing verified recovery result'
 $results.Add(@{fault=$stage;prior_files_and_state='restored and hash-verified'})
}
$empty=Join-Path $OutputRoot 'missing-runtime';New-Item -ItemType Directory -Path $empty | Out-Null
$run=Invoke-Install $empty 'DX11'
$logText=Get-Content -LiteralPath $run.log -Raw
Assert-True ($run.code -ne 0 -and $logText.Contains('Supply -RuntimePath') -and -not $logText.Contains('NonInteractive mode')) 'Missing-runtime error attempted interaction'
$results.Add(@{missing_runtime='actionable noninteractive error'})
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputRoot 'results.json') -Encoding UTF8
$results | ConvertTo-Json -Depth 6
