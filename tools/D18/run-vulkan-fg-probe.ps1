param(
 [Parameter(Mandatory)][string]$Executable,
 [Parameter(Mandatory)][string]$RuntimeDirectory,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [ValidateSet('--lifecycle','--device-only','--present','--present-visible')][string]$Stage='--device-only',
 [ValidateRange(5,60)][int]$TimeoutSeconds=15
)
$ErrorActionPreference='Stop'
$probePath=(Resolve-Path -LiteralPath $Executable).Path
$runtimePath=(Resolve-Path -LiteralPath $RuntimeDirectory).Path
$outPath=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $outPath){throw 'Use a new output directory to preserve evidence'}
New-Item -ItemType Directory -Path $outPath | Out-Null
$arguments='"'+$runtimePath+'" "'+(Join-Path $outPath 'logs')+'" '+$Stage
$windowStyle=if($Stage -eq '--present-visible'){'Normal'}else{'Hidden'}
$probe=Start-Process -FilePath $probePath -ArgumentList $arguments -WindowStyle $windowStyle -PassThru -RedirectStandardOutput (Join-Path $outPath 'stdout.txt') -RedirectStandardError (Join-Path $outPath 'stderr.txt')
$timedOut=!$probe.WaitForExit($TimeoutSeconds*1000)
if($timedOut){
 if(!$probe.HasExited){if($probe.Path -ne $probePath){throw 'Unexpected process identity'};$probe.Kill();$probe.WaitForExit()}
}
[ordered]@{executable=$probePath;runtime=$runtimePath;stage=$Stage;pid=$probe.Id;timeout=$timedOut;exit_code=$probe.ExitCode;timeout_seconds=$TimeoutSeconds;scope='isolated host; no game mutation'} | ConvertTo-Json | Set-Content (Join-Path $outPath 'result.json') -Encoding utf8
Get-Content (Join-Path $outPath 'stdout.txt') -Tail 12
if($timedOut){exit 124};exit $probe.ExitCode
