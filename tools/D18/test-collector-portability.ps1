[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ToolsDirectory,[Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $FixtureRoot){throw 'Use a new fixture root'}
$game=Join-Path $FixtureRoot 'game 中文 space'
[IO.Directory]::CreateDirectory($game) | Out-Null
[IO.File]::WriteAllText((Join-Path $game 'D24Native.log'),"{`"event`":`"skip_or_failure`",`"value`":-30}`n")
[IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),"[DLSSNR]`nEnabled=false`nDiagnostics=0`n")
$before=(Get-FileHash -LiteralPath (Join-Path $game 'OptiScaler.ini')).Hash
$rows=@()
foreach($entry in @('collect-diagnostics.ps1','collect-render-diagnostics.ps1')){
    $out=Join-Path $FixtureRoot ($entry+'.output 中文')
    & (Join-Path $ToolsDirectory $entry) -GameDirectory $game -OutputDirectory $out | Out-Null
    if(!(Test-Path -LiteralPath (Join-Path $out 'native-nr-summary.json'))){throw 'Missing native adapter summary'}
    $hash=(Get-FileHash -LiteralPath (Join-Path $out 'collection.json')).Hash
    $rejected=$false
    try {& (Join-Path $ToolsDirectory $entry) -GameDirectory $game -OutputDirectory $out | Out-Null} catch {$rejected=$_.Exception.Message -like '*already exists*'}
    if(!$rejected -or $hash -ne (Get-FileHash -LiteralPath (Join-Path $out 'collection.json')).Hash){throw 'Existing evidence was not protected'}
    $rows+=@{entry=$entry;explicit_path=$out;collected=$true;existing_output_preserved=$true}
}
. (Join-Path $ToolsDirectory 'diagnostic-output.ps1')
$rejected=$false
try {New-D18DiagnosticDirectory -OutputDirectory (Join-Path $game 'inside-source') -SourceDirectories @($game) | Out-Null} catch {$rejected=$_.Exception.Message -like '*outside the game*'}
if(!$rejected){throw 'Source guard missing'}
$default=New-D18DiagnosticDirectory -SourceDirectories @($game)
if(!(Test-Path -LiteralPath $default) -or $default -like 'E:\DLSSNR\evidence\*'){throw 'Default is not portable'}
if((Get-ChildItem -LiteralPath $default -Force).Count){throw 'Unexpected content in new default directory'}
[IO.Directory]::Delete($default,$false) # Just the empty directory created above.
if($before -ne (Get-FileHash -LiteralPath (Join-Path $game 'OptiScaler.ini')).Hash){throw 'Source configuration changed'}
@{cases=$rows;default_output=$default;source_preserved=$true;source_output_rejected=$true} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $FixtureRoot 'results.json') -Encoding utf8
Write-Output 'Collector portability and preservation PASS'
