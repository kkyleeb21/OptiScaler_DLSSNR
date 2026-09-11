# Run in a Visual Studio 2022 x64 Developer PowerShell with the Windows SDK installed.
[CmdletBinding()]
param([string]$DependencyRoot,[string]$OutputDirectory,[switch]$AddonOnly)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if(-not $DependencyRoot){$DependencyRoot=$repo}
$DependencyRoot=(Resolve-Path -LiteralPath $DependencyRoot).Path.TrimEnd('\')+'\'
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repo 'build-d18'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')+'\'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
if(-not (Test-Path (Join-Path $DependencyRoot 'external\nvngx_dlss_sdk\nvsdk_ngx.h'))){throw 'Missing NGX headers; initialize the documented dependencies first.'}
if(-not $AddonOnly){
 & msbuild (Join-Path $repo 'OptiScaler\OptiScaler.vcxproj') /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$DependencyRoot" "/p:OutDir=$OutputDirectory" "/p:IntDir=${OutputDirectory}obj\" /p:PostBuildEventUseInBuild=false /p:PreBuildEventUseInBuild=false /m:2 /nologo /verbosity:minimal
 if($LASTEXITCODE){throw 'Core build failed'}
 & msbuild (Join-Path $repo 'OptiScaler\dlssnr\forwarder\dlssnr_forwarder.vcxproj') /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$DependencyRoot" "/p:OutDir=$OutputDirectory" "/p:IntDir=${OutputDirectory}forwarder-obj\" /nologo /verbosity:minimal
 if($LASTEXITCODE){throw 'Forwarder build failed'}
}
& cl /nologo /LD /W4 /WX /EHa /std:c++17 "/I$repo\OptiScaler" "/I${DependencyRoot}external\nvngx_dlss_sdk" (Join-Path $PSScriptRoot 'bg3-native\D24Native.cpp') "/Fe:${OutputDirectory}D24Native.dll" "/Fo:${OutputDirectory}D24Native.obj"
if($LASTEXITCODE){throw 'Native addon build failed'}
& cl /nologo /O2 /W4 /WX /EHsc /std:c++17 (Join-Path $PSScriptRoot 'runtime-guard\main.cpp') "/Fe:${OutputDirectory}D18RuntimeCheck.exe" "/Fo:${OutputDirectory}D18RuntimeCheck.obj"
if($LASTEXITCODE){throw 'Runtime checker build failed'}
Write-Host "D18 output: $OutputDirectory"
