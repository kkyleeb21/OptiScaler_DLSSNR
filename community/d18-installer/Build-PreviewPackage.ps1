#Requires -Version 5.1
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$OriginalPackage,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [string]$Core, [string]$Forwarder, [string]$DxcRoot,
 [switch]$OnimushaOnly
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'D18-Common.ps1')
if(Test-Path -LiteralPath $OutputDirectory){throw 'Output must be a new directory.'}
$originalManifest=Test-D18Payload -PayloadRoot (Join-Path $OriginalPackage 'payload') -ManifestPath (Join-Path $OriginalPackage 'payload_manifest.json')
if((Get-D18Sha256 (Join-Path $OriginalPackage 'payload\OptiScaler.dll')) -ne 'C3E8F20F5AD48248E78B3B847DB25463C4214D0A81A99C1022452D420CE1A507'){throw 'Not the published original 0.1.1 final-UI core.'}
Copy-Item -LiteralPath $OriginalPackage -Destination $OutputDirectory -Recurse
foreach($name in @('D18-Common.ps1','Install-D18.ps1','Uninstall-D18.ps1','README_CN.md')) {
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $OutputDirectory $name) -Force
}
$payload=Join-Path $OutputDirectory 'payload'
if($OnimushaOnly) {
 foreach($path in @($Core,$Forwarder,(Join-Path $DxcRoot 'bin\x64\dxcompiler.dll'))) {
  if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing dependency: $path"}
 }
 Copy-Item -LiteralPath $Core -Destination (Join-Path $payload 'OptiScaler.dll') -Force
 Copy-Item -LiteralPath $Forwarder -Destination (Join-Path $payload 'nvngx.dll_dlssnr.dll') -Force
 $privateDxc=Join-Path $payload 'OptiScaler\D18'
 New-Item -ItemType Directory -Path $privateDxc -Force | Out-Null
 # Reflection only: no DXIL validator is required or bundled.
 Copy-Item -LiteralPath (Join-Path $DxcRoot 'bin\x64\dxcompiler.dll') -Destination $privateDxc
 foreach($license in @('LICENSE-MIT.txt','LICENSE-LLVM.txt','LICENSE-MS.txt')) {
  Copy-Item -LiteralPath (Join-Path $DxcRoot $license) -Destination (Join-Path $payload ('Licenses\DXC-'+$license))
 }
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Onimusha-Profile.ps1') -Destination $OutputDirectory
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ONIMUSHA_README.md') -Destination (Join-Path $OutputDirectory 'README_CN.md') -Force
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ONIMUSHA_README.md') -Destination (Join-Path $OutputDirectory 'README.md') -Force
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Onimusha-default.ini') -Destination (Join-Path $payload 'OptiScaler.ini.d18') -Force
}
if(Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File -Filter 'nvngx_dlssnr.dll'){throw 'NVIDIA Runtime must never be bundled.'}
$entries=@(Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName | ForEach-Object {
 [ordered]@{path=$_.FullName.Substring($payload.Length+1);size=$_.Length;sha256=(Get-D18Sha256 $_.FullName)}
})
$name=if($OnimushaOnly){'D18 0.1.1 - Onimusha ONLY Preview'}else{'D18 0.1.1 - Installer prompts update'}
$manifest=[ordered]@{format='dlssnr-d18-payload-manifest-v1'; release_name=$name;release_version='0.1.1';contains_nvidia_runtime=$false;source_commit=((& git -C $PSScriptRoot rev-parse HEAD) + ' + local candidate');files=$entries}
if($OnimushaOnly){$manifest.game_profile='onimusha-only'}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'payload_manifest.json'),($manifest|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
$sums=@(Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object FullName | ForEach-Object { (Get-D18Sha256 $_.FullName)+'  '+$_.FullName.Substring($OutputDirectory.Length+1) })
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS.txt'),$sums)
Compress-Archive -LiteralPath $OutputDirectory -DestinationPath ($OutputDirectory+'.zip')
Write-Output ($OutputDirectory+'.zip')
