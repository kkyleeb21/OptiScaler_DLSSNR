#Requires -Version 5.1
param([string]$ScratchRoot = [IO.Path]::GetTempPath())
$ErrorActionPreference = 'Stop'
$source = Split-Path -Parent $PSScriptRoot
. (Join-Path $source 'D18-Common.ps1')
$fixture = Join-Path $ScratchRoot ('D18-common-install-test-'+[guid]::NewGuid().ToString('N'))
$package = Join-Path $fixture 'package'
$payload = Join-Path $package 'payload'
$game = Join-Path $fixture 'game'
New-Item -ItemType Directory -Path $payload,$game | Out-Null
foreach ($name in @('Install-D18.ps1','Uninstall-D18.ps1','D18-Common.ps1')) {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination $package
}
# Synthetic package and Runtime; no NVIDIA/game binaries are needed or redistributed.
[IO.File]::WriteAllBytes((Join-Path $payload 'OptiScaler.dll'), [byte[]](1,2,3))
[IO.File]::WriteAllText((Join-Path $payload 'OptiScaler.ini.d18'), "[Menu]`r`nShortcutKey=auto`r`n[DlssNr]`r`nEnabled=true`r`n")
$runtime = Join-Path $fixture 'runtime.dll'
[IO.File]::WriteAllBytes($runtime, [byte[]](1,2,3,4))
$patched = Join-Path $fixture 'patched.dll'
[IO.File]::WriteAllBytes($patched, [byte[]](1,20,30,4))
$patch = @{format='dlssnr-d18-guarded-layout-patch-v2';reference_source_sha256=(Get-D18Sha256 $runtime)
    reference_output_sha256=(Get-D18Sha256 $patched)
    hunks=@(@{offset=1;expected_base64='AgM=';replacement_base64='FB4=';compatible_input_base64=@()})}
[IO.File]::WriteAllText((Join-Path $package 'runtime_patch.json'), ($patch | ConvertTo-Json -Depth 6))
$files = @('OptiScaler.dll','OptiScaler.ini.d18') | ForEach-Object {
    @{path=$_;sha256=(Get-D18Sha256 (Join-Path $payload $_))}
}
[IO.File]::WriteAllText((Join-Path $package 'payload_manifest.json'),
    (@{release_name='Synthetic test';release_version='0.1.1';files=@($files)} | ConvertTo-Json -Depth 6))
$shell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
function Install([string]$Key) {
    $argsList=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $package 'Install-D18.ps1'),
        '-GameDir',$game,'-RuntimePath',$runtime,'-ProxyName','winmm.dll','-Yes')
    if ($Key) { $argsList += @('-UiToggleKey',$Key) }
    & $shell @argsList | Out-Null
    if ($LASTEXITCODE) { throw 'Synthetic install failed' }
}
Install 'F10'
$ini = Join-Path $game 'OptiScaler.ini'
if ((Get-D18UiKey ([IO.File]::ReadAllText($ini))) -ne '121') { throw 'Fresh F10 not applied' }
if (-not (Test-Path -LiteralPath (Join-Path $game 'winmm.dll'))) { throw 'Generic proxy selection lost' }
# Existing full configuration and both keys must survive replacement, even with a new CLI key.
[IO.File]::AppendAllText($ini, "ToggleKey=120`r`nLumaTrust=1.25`r`n[CustomGame]`r`nKeepMe=yes`r`n")
$before = [IO.File]::ReadAllText($ini)
Install 'Home'
if ([IO.File]::ReadAllText($ini) -cne $before) { throw 'Upgrade modified existing INI' }
& $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $package 'Uninstall-D18.ps1') -GameDir $game -Yes | Out-Null
if ($LASTEXITCODE) { throw 'Uninstall failed' }
# Safe uninstaller may preserve the user-modified INI; a separate fresh directory tests -Yes default.
$game = Join-Path $fixture 'default-game'
New-Item -ItemType Directory -Path $game | Out-Null
Install ''
if ((Get-D18UiKey ([IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini')))) -ne '45') { throw 'Default is not Insert' }
Write-Output "PASS first F10, generic winmm proxy, entire INI upgrade preservation, uninstall, default Insert. Fixture: $fixture"
