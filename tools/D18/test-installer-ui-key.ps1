param([string]$PackageSource)
$ErrorActionPreference='Stop'
$source='E:\DLSSNR\workspace\dlss5\worktrees\d18-011-onimusha-release\community\d18-installer'
$fixture=Join-Path 'E:\DLSSNR\builds' ('UiKeyInstallerTest-'+[guid]::NewGuid().ToString('N'))
$package=Join-Path $fixture 'package'
$game=Join-Path $fixture 'game'
New-Item -ItemType Directory -Path $fixture,$game | Out-Null
if($PackageSource) { Copy-Item -LiteralPath $PackageSource -Destination $package -Recurse }
else {
    Copy-Item -LiteralPath 'E:\DLSSNR\releases\D18_0.1.1_Onimusha_ONLY_RC3' -Destination $package -Recurse
    foreach($file in @('Install-D18.ps1','D18-Common.ps1')) { Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $package $file) -Force }
}
[IO.File]::WriteAllBytes((Join-Path $game 'OnimushaWotS.exe'),[byte[]](0))
foreach($file in @('dinput8.dll','nvngx_dlssnr.dll')) { Copy-Item -LiteralPath (Join-Path 'D:\SteamLibrary\steamapps\common\OnimushaWotS' $file) -Destination $game }
$shell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
. (Join-Path $source 'D18-Common.ps1')
& $shell -NoProfile -ExecutionPolicy Bypass -File "$package\Install-D18.ps1" -GameDir $game -Yes -UiToggleKey F10
if($LASTEXITCODE){throw 'Fresh install failed'}
foreach($relative in @('OptiScaler.ini','_storage_\OptiScaler.ini')) {
    if((Get-D18UiKey ([IO.File]::ReadAllText((Join-Path $game $relative)))) -ne '121'){throw 'Fresh key mirror mismatch'}
}
& $shell -NoProfile -ExecutionPolicy Bypass -File "$package\Install-D18.ps1" -GameDir $game -Yes -UiToggleKey Home
if($LASTEXITCODE){throw 'Upgrade failed'}
foreach($relative in @('OptiScaler.ini','_storage_\OptiScaler.ini')) {
    if((Get-D18UiKey ([IO.File]::ReadAllText((Join-Path $game $relative)))) -ne '121'){throw 'Upgrade changed key'}
}
& $shell -NoProfile -ExecutionPolicy Bypass -File "$package\Uninstall-D18.ps1" -GameDir $game -Yes
if($LASTEXITCODE){throw 'Uninstall failed'}
if(Test-Path -LiteralPath "$game\OptiScaler.ini"){throw 'Fresh INI not removed'}
& $shell -NoProfile -ExecutionPolicy Bypass -File "$package\Install-D18.ps1" -GameDir $game -Yes
if($LASTEXITCODE){throw 'Default install failed'}
if((Get-D18UiKey ([IO.File]::ReadAllText("$game\OptiScaler.ini"))) -ne '45'){throw 'Default not Insert'}
Write-Output "PASS fresh F10 + mirrors, upgrade preservation, uninstall, default Insert. Retained fixture: $fixture"
