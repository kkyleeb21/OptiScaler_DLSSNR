$ErrorActionPreference='Stop'
$root='E:\DLSSNR'
$source=Join-Path $root 'workspace\dlss5\worktrees\d18-012-re-integration\community\d18-installer'
. (Join-Path $source 'D18-Common.ps1')
$fixture=Join-Path $root ('builds\proxy-install-test-'+[guid]::NewGuid().ToString('N'))
$package=Join-Path $fixture 'package';$game=Join-Path $fixture 'game'
New-Item -ItemType Directory -Path $package,$game,(Join-Path $package 'payload') | Out-Null
foreach($n in @('Install-D18.ps1','Uninstall-D18.ps1','D18-Common.ps1','D18-REFramework.ps1','runtime_patch.json','reframework-versions.json')){Copy-Item -LiteralPath (Join-Path $source $n) -Destination $package}
foreach($n in @('OptiScaler.dll','D24Native.dll')){Set-Content -LiteralPath (Join-Path $package "payload\$n") -Value "fixture-$n"}
Set-Content -LiteralPath (Join-Path $package 'payload\OptiScaler.ini.d18') -Value "[DlssNr]`nEnabled=true"
$payload=Join-Path $package 'payload'
@{format='dlssnr-d18-payload-manifest-v1';files=@(Get-ChildItem $payload -File | ForEach-Object {@{path=$_.Name;sha256=(Get-FileHash $_.FullName).Hash}})} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $package 'payload_manifest.json')
Set-Content (Join-Path $game 'D18InstallerFixture.exe') 'fixture only; never executed'
Set-Content (Join-Path $game 'version.dll') 'existing plugin'
Set-Content (Join-Path $game 'OptiScaler.ini') "[User]`nSentinel=keep"
$plugin=(Get-FileHash (Join-Path $game 'version.dll')).Hash
$config=(Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash
function Check($condition,$message){if(-not $condition){throw $message}}
foreach($name in @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll')){Check ((Select-D18ProxyName -Requested $name -Recommended 'dxgi.dll' -AssumeYes) -eq $name) 'explicit choice'}
Check ((Select-D18ProxyName -Previous 'version.dll' -Recommended 'dxgi.dll' -AssumeYes) -eq 'version.dll') 'upgrade preference'
Set-Content (Join-Path $game 'Endfield.exe') 'fixture'
Check ((Get-D18ProxyRecommendation $game).Name -eq 'd3d12.dll') 'Endfield recommendation'
# File is a test fixture inside the verified test root; remove only this named file.
Remove-Item -LiteralPath (Join-Path $game 'Endfield.exe')
$runtime=Join-Path $root 'workspace\dlss5\extracted\DLSS310.8.0-Streamline2.13\nvngx_dlssnr.dll'
function Install([string[]]$extra){
 & pwsh -NoProfile -File (Join-Path $package 'Install-D18.ps1') -GameDir $game -RuntimePath $runtime -NativeApi DX11 -Yes @extra *> (Join-Path $fixture 'install.log')
 Check ($LASTEXITCODE -eq 0) 'install failed; inspect install.log'
}
Install @('-ProxyName','version.dll')
$state=Get-Content (Join-Path $game '.dlssnr-d18-install.json') -Raw | ConvertFrom-Json
Check ($state.proxy_name -eq 'version.dll' -and $state.native_api -eq 'DX11') 'initial profile'
Check (Test-Path (Join-Path $game 'D24Runtime.dll')) 'native runtime destination'
Install @()
$state=Get-Content (Join-Path $game '.dlssnr-d18-install.json') -Raw | ConvertFrom-Json
Check ($state.proxy_name -eq 'version.dll') 'name lost on upgrade'
Install @('-ProxyName','winmm.dll')
Check ((Get-FileHash (Join-Path $game 'version.dll')).Hash -eq $plugin) 'old plugin not restored on rename'
& pwsh -NoProfile -File (Join-Path $package 'Uninstall-D18.ps1') -GameDir $game -Yes *> (Join-Path $fixture 'uninstall.log')
Check ($LASTEXITCODE -eq 0) 'uninstall failed'
Check (-not (Test-Path (Join-Path $game 'winmm.dll'))) 'selected proxy not removed'
Check ((Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash -eq $config) 'configuration changed'
Check ((Get-FileHash (Join-Path $game 'version.dll')).Hash -eq $plugin) 'plugin changed'
Move-Item -LiteralPath (Join-Path $game 'OptiScaler.ini') -Destination (Join-Path $fixture 'original.ini')
Install @()
$fresh=Get-Content (Join-Path $game 'OptiScaler.ini') -Raw
Check ($fresh -match '(?m)^Dx11Upscaler=dlss' -and $fresh -match '(?m)^ToggleKey=33') 'fresh native routing or PgUp missing'
$priorStateHash=(Get-FileHash (Join-Path $game '.dlssnr-d18-install.json')).Hash
$unknownRuntime=Join-Path $fixture 'layout-compatible-unknown-runtime.dll'
Copy-Item -LiteralPath $runtime -Destination $unknownRuntime
$stream=[IO.File]::Open($unknownRuntime,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite);try{$stream.Position=0x50;$value=$stream.ReadByte();$stream.Position=0x50;$stream.WriteByte($value -bxor 1)}finally{$stream.Dispose()}
& pwsh -NoProfile -File (Join-Path $package 'Install-D18.ps1') -GameDir $game -RuntimePath $unknownRuntime -NativeApi DX11 -ProxyName dbghelp.dll -Yes *> (Join-Path $fixture 'runtime-rejected.log')
Check ($LASTEXITCODE -ne 0) 'DX11 accepted runtime outside backend whitelist'
Check ((Get-Content (Join-Path $fixture 'runtime-rejected.log') -Raw) -match 'not accepted by the DX11 backend') 'wrong runtime rejection reason'
Check ((Get-FileHash (Join-Path $game '.dlssnr-d18-install.json')).Hash -eq $priorStateHash) 'runtime preflight changed previous install'
Check (-not (Test-Path (Join-Path $game 'dbghelp.dll'))) 'proxy installed before runtime rejection'
"PASS: five names, recommendation, install, upgrade retention, rename restoration, uninstall, configuration; $fixture"
