# Run in a Visual Studio 2022 x64 Developer PowerShell with the Windows SDK installed.
[CmdletBinding()]
param([string]$DependencyRoot,[string]$OutputDirectory,[switch]$AddonOnly,[ValidateSet('Release','Diagnostic')][string]$Profile='Release')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if(-not $DependencyRoot){$DependencyRoot=$repo}
$DependencyRoot=(Resolve-Path -LiteralPath $DependencyRoot).Path.TrimEnd('\')+'\'
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repo 'build-d18'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')+'\'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
if(-not (Test-Path (Join-Path $DependencyRoot 'external\nvngx_dlss_sdk\nvsdk_ngx.h'))){throw 'Missing NGX headers; initialize the documented dependencies first.'}
$diagnostic=if($Profile -eq 'Diagnostic'){1}else{0}
& python (Join-Path $PSScriptRoot 'runtime-guard\generate-patch-sites.py') --check
if($LASTEXITCODE){throw 'Runtime patch tables are stale; regenerate before building both validators.'}
if($diagnostic -eq 1 -and -not (Test-Path (Join-Path $repo 'OptiScaler\dlssnr\Dx11FocusedShaders.h'))){throw 'Diagnostic source is incomplete.'}
if(-not $AddonOnly){
 & msbuild (Join-Path $repo 'OptiScaler\OptiScaler.vcxproj') /p:Configuration=Release /p:Platform=x64 "/p:D18DiagnosticBuild=$diagnostic" "/p:SolutionDir=$DependencyRoot" "/p:OutDir=$OutputDirectory" "/p:IntDir=${OutputDirectory}obj\" /p:PostBuildEventUseInBuild=false /p:PreBuildEventUseInBuild=false /m:2 /nologo /verbosity:minimal
 if($LASTEXITCODE){throw 'Core build failed'}
 & msbuild (Join-Path $repo 'OptiScaler\dlssnr\forwarder\dlssnr_forwarder.vcxproj') /p:Configuration=Release /p:Platform=x64 "/p:D18DiagnosticBuild=$diagnostic" "/p:SolutionDir=$DependencyRoot" "/p:OutDir=$OutputDirectory" "/p:IntDir=${OutputDirectory}forwarder-obj\" /nologo /verbosity:minimal
 if($LASTEXITCODE){throw 'Forwarder build failed'}
}
# Release identity only; renderer and patch admission code are unchanged.
& rc /nologo /d D18_VERSION_DLL "/fo${OutputDirectory}D24Native-version.res" (Join-Path $PSScriptRoot 'd18-version.rc')
if($LASTEXITCODE){throw 'Native version resource build failed'}
& rc /nologo "/fo${OutputDirectory}D18RuntimeCheck-version.res" (Join-Path $PSScriptRoot 'd18-version.rc')
if($LASTEXITCODE){throw 'Checker version resource build failed'}
& cl /nologo /utf-8 /LD /O2 /EHsc /std:c++20 "/DD18_DIAGNOSTIC_BUILD=$diagnostic" "/I$repo\OptiScaler" "/I${DependencyRoot}external\nvngx_dlss_sdk" (Join-Path $PSScriptRoot 'bg3-native\D24Native.cpp') "/Fe:${OutputDirectory}D24Native.dll" "/Fo:${OutputDirectory}D24Native.obj" /link "/DEF:$PSScriptRoot\bg3-native\exports.def" "${OutputDirectory}D24Native-version.res"
if($LASTEXITCODE){throw 'Native addon build failed'}
& cl /nologo /utf-8 /O2 /W4 /WX /EHsc /std:c++17 (Join-Path $PSScriptRoot 'runtime-guard\main.cpp') "/Fe:${OutputDirectory}D18RuntimeCheck.exe" "/Fo:${OutputDirectory}D18RuntimeCheck.obj" /link "${OutputDirectory}D18RuntimeCheck-version.res"
if($LASTEXITCODE){throw 'Runtime checker build failed'}
Write-Host "D18 output: $OutputDirectory"
