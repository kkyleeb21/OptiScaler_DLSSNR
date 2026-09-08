param(
 [Parameter(Mandatory=$true)][string]$PackageZip,
 [Parameter(Mandatory=$true)][string]$Runtime
)
$ErrorActionPreference='Stop'
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('release-install-review-'+[guid]::NewGuid().ToString('N'))
$package=Join-Path $fixture 'package'
New-Item -ItemType Directory -Path $package | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($PackageZip,$package)
. (Join-Path $package 'D18-Common.ps1')
$manifest=Get-Content -LiteralPath (Join-Path $package 'payload_manifest.json') -Raw | ConvertFrom-Json
$cases=@()
foreach($api in @('DX11','Vulkan','None')){
 $game=Join-Path $fixture $api;New-Item -ItemType Directory -Path $game | Out-Null
 Set-Content -LiteralPath (Join-Path $game 'D18ReleaseFixture.exe') -Value 'Non-executable test fixture, never launched'
 Set-Content -LiteralPath (Join-Path $game 'dxgi.dll') -Value 'Original proxy sentinel, never loaded'
 $originalProxy=(Get-FileHash -LiteralPath (Join-Path $game 'dxgi.dll')).Hash
 $install=Join-Path $package 'Install-D18.ps1'
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $install -GameDir $game -RuntimePath $Runtime -NativeApi $api -ProxyName dxgi.dll -Yes *> (Join-Path $fixture "$api-install.log")
 if($LASTEXITCODE -ne 0){throw "$api install failed: $fixture"}
 $runtimeName=if($api -eq 'None'){'nvngx_dlssnr.dll'}else{'D24Runtime.dll'}
 if((Get-FileHash -LiteralPath (Join-Path $game $runtimeName)).Hash -ne 'CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414'){throw 'Generated runtime mismatch'}
 foreach($entry in $manifest.files){
  if($entry.path -eq 'OptiScaler.ini.d18'){continue}
  if($entry.path -eq 'D24Native.dll' -and $api -ne 'DX11'){continue}
  if($entry.path -eq 'D24VulkanNR.enabled' -and $api -ne 'Vulkan'){continue}
  $target=Get-D18TargetRelativePath -PayloadRelativePath $entry.path -ProxyName dxgi.dll
  if((Get-FileHash -LiteralPath (Join-Path $game $target)).Hash -ne $entry.sha256){throw "Installed payload mismatch: $api $target"}
 }
 if((Test-Path -LiteralPath (Join-Path $game 'D24Native.dll')) -ne ($api -eq 'DX11')){throw 'Addon API mapping mismatch'}
 if((Test-Path -LiteralPath (Join-Path $game 'D24VulkanNR.enabled')) -ne ($api -eq 'Vulkan')){throw 'Vulkan activation mismatch'}
 $ini=Join-Path $game 'OptiScaler.ini'
 $text=[IO.File]::ReadAllText($ini)
 if($text -notmatch '(?m)^Diagnostics=0\r?$' -or $text -notmatch '(?m)^AutoCapture=false\r?$'){throw 'Diagnostic defaults mismatch'}
 $text=Set-D18IniValue -Text $text -Section DlssNr -Key JitterCorrection -Value false
 $text=Set-D18IniValue -Text $text -Section Menu -Key D18Language -Value 1
 [IO.File]::WriteAllText($ini,$text,[Text.UTF8Encoding]::new($false))
 $configHash=(Get-FileHash -LiteralPath $ini).Hash
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $install -GameDir $game -RuntimePath $Runtime -Yes *> (Join-Path $fixture "$api-upgrade.log")
 if($LASTEXITCODE -ne 0){throw "$api upgrade failed: $fixture"}
 if((Get-FileHash -LiteralPath $ini).Hash -ne $configHash){throw 'Upgrade changed user configuration'}
 $state=Get-Content -LiteralPath (Join-Path $game '.dlssnr-d18-install.json') -Raw | ConvertFrom-Json
 if($state.native_api -ne $api -or $state.proxy_name -ne 'dxgi.dll'){throw 'Upgrade lost API or proxy choice'}
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $package 'Uninstall-D18.ps1') -GameDir $game -Yes *> (Join-Path $fixture "$api-uninstall.log")
 if($LASTEXITCODE -ne 0){throw "$api uninstall failed: $fixture"}
 if((Get-FileHash -LiteralPath (Join-Path $game 'dxgi.dll')).Hash -ne $originalProxy){throw 'Original proxy not restored'}
 if(Test-Path -LiteralPath (Join-Path $game $runtimeName)){throw 'Installed runtime not removed'}
 $cases+=@{api=$api;install='pass';upgrade='pass';explicit_jitter_false_preserved=$true;chinese_language_preserved=$true;uninstall='pass';payload_hashes='pass';shell='Windows PowerShell 5.1'}
}
$result=@{fixture=$fixture;zip_sha256=(Get-FileHash -LiteralPath $PackageZip).Hash;cases=$cases;gameplay_test=$false}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $fixture 'result.json') -Encoding utf8
$result | ConvertTo-Json -Depth 6
