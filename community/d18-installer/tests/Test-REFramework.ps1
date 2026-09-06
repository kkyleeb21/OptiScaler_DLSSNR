#Requires -Version 5.1
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'D18-Common.ps1')
. (Join-Path $root 'D18-REFramework.ps1')
function Assert($Condition,$Message) { if (-not $Condition) { throw $Message } }
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('D18-REF-test-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$profile=Get-D18ReProfile $fixture
Assert (-not $profile.IsRE) 'Ordinary game misidentified'
[IO.File]::WriteAllText((Join-Path $fixture 'PRAGMATA.exe'),'fixture')
$profile=Get-D18ReProfile $fixture
Assert ($profile.IsRE -and $profile.Tested) 'Matched game not detected'
$text="Other=keep`nREFrameworkConfig_MenuKey_V2=45`nPlugin_Enabled=true`n"
$result=Set-D18RefMenuKey $text
Assert ($result -eq $text.Replace('MenuKey_V2=45','MenuKey_V2=34')) 'Other REF settings changed'
Assert ((Set-D18RefMenuKey '') -match 'MenuKey_V2=34') 'Missing PgDn default'
$release=[pscustomobject]@{assets=@([pscustomobject]@{name='REFramework.zip';browser_download_url='https://github.com/praydog/REFramework-nightly/releases/download/test/REFramework.zip';digest=('sha256:'+('a'*64))})}
$null=Select-D18RefAsset $release
$release.assets[0].browser_download_url='https://github.com/praydog/REFramework/releases/download/test/REFramework.zip'
$rejected=$false;try{$null=Select-D18RefAsset $release}catch{$rejected=$true}
Assert $rejected 'Wrong repository accepted'
$release.assets[0].browser_download_url='https://github.com/praydog/REFramework-nightly/releases/download/test/REFramework.zip'
$release.assets[0].digest=$null
$rejected=$false;try{$null=Select-D18RefAsset $release}catch{$rejected=$true}
Assert $rejected 'Missing digest accepted'
$stage=Join-Path $fixture 'stage';New-Item -ItemType Directory -Path $stage | Out-Null
$items=New-Object System.Collections.Generic.List[object]
$items.Add([pscustomobject]@{Source='core';TargetRelative='d3d12.dll';ExpectedHash='hash'})
$items.Add([pscustomobject]@{Source='ini';TargetRelative='OptiScaler.ini';ExpectedHash='hash'})
Add-D18RefItems -Game $fixture -Profile $profile -Mode Manual -Stage $stage -Items $items -AssumeYes
Assert (@($items | Where-Object TargetRelative -eq '_storage_\d3d12.dll').Count -eq 1) 'Mirror missing'
Assert (@($items | Where-Object TargetRelative -eq '_storage_\OptiScaler.ini').Count -eq 0) 'Duplicate INI storage'
[IO.File]::WriteAllText((Join-Path $fixture 'dinput8.dll'),'another-loader')
$rejected=$false;try{Add-D18RefItems -Game $fixture -Profile $profile -Mode Auto -Stage $stage -Items $items -AssumeYes}catch{$rejected=$true}
Assert $rejected 'Unrecognized loader overwritten'
Write-Output "PASS RE detection, official source/digest rejection, PgDn merge, mirror/single INI, loader preservation. Fixture: $fixture"
