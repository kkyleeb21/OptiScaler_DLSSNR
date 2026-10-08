#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$ScratchRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\D18-Common.ps1')
$checks=0
function Assert($ok,$label){if(-not $ok){throw "FAIL $label"};$script:checks++;Write-Output "PASS $label"}
# Root cases are strings only; never create, enumerate or write a real root.
foreach($p in @('C:\','d:/','Z:\\','\\server\share','\\server\share\','//server/share/','\\?\C:\','\\?\UNC\server\share\')){
 Assert (Test-D18PathRoot $p) "root string: $p"
}
foreach($p in @('C:\Game','D:\WindowsGames','\\server\share\Game','C:\Program Files\Game')){
 Assert (-not (Test-D18PathRoot $p)) "non-root string: $p"
}
$scratch=Join-Path ([IO.Path]::GetFullPath($ScratchRoot)) ('D18-scope-tests-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $scratch
$savedWindows=$env:SystemRoot
$savedHome=$env:USERPROFILE
$productionProtected=(Get-Item function:Get-D18ProtectedDirectories).ScriptBlock
# Protected directory identities are replaced with test-owned paths only.
$fakeWindows=Join-Path $scratch 'system\Windows'
$fakeProgram=Join-Path $scratch 'program\Program Files'
$fakeProgram86=Join-Path $scratch 'program86\Program Files (x86)'
$fakeHome=Join-Path $scratch 'users\Fixture'
$desktop=Join-Path $fakeHome 'Desktop'
$downloads=Join-Path $fakeHome 'Downloads'
$documents=Join-Path $fakeHome 'Documents'
$pictures=Join-Path $fakeHome 'Pictures';$music=Join-Path $fakeHome 'Music';$videos=Join-Path $fakeHome 'Videos'
function Get-D18ProtectedDirectories { @($fakeWindows,$fakeProgram,$fakeProgram86,$fakeHome,$desktop,$downloads,$documents,$pictures,$music,$videos) }
try {
 $env:SystemRoot=$fakeWindows
 foreach($p in @($fakeWindows,$fakeProgram,$fakeProgram86,$fakeHome,$desktop,$downloads,$documents,$pictures,$music,$videos,(Join-Path $fakeWindows 'System32'))){
  $null=New-Item -ItemType Directory -Path $p -Force
  [IO.File]::WriteAllText((Join-Path $p 'Sentinel.exe'),'inert, never executed')
  $r=Get-D18GameDirectoryScope $p
  Assert ($r.code -eq 'protected_directory' -and $r.zh -and $r.en) "protected with EXE: $p"
 }
 foreach($p in @((Split-Path -Parent $fakeWindows),(Split-Path -Parent $fakeProgram),(Split-Path -Parent $fakeProgram86),(Split-Path -Parent $fakeHome))){
  Assert ((Get-D18GameDirectoryScope $p).code -eq 'protected_directory') "protected ancestor: $p"
 }
 foreach($p in @((Join-Path $scratch 'Unknown Game'),(Join-Path $fakeProgram 'Unknown Game'),(Join-Path $fakeProgram86 'Unknown Game'),(Join-Path $desktop 'Game'),(Join-Path $downloads 'Game'),(Join-Path $documents 'Game'),(Join-Path $scratch 'WindowsGames'))){
  $null=New-Item -ItemType Directory -Path $p -Force
  [IO.File]::WriteAllText((Join-Path $p 'Anything.EXE'),'inert, never executed')
  Assert ((Get-D18GameDirectoryScope $p).allowed) "generic game subdirectory allowed: $p"
 }
 $empty=Join-Path $scratch 'Empty';$null=New-Item -ItemType Directory -Path (Join-Path $empty 'nested') -Force
 [IO.File]::WriteAllText((Join-Path $empty 'nested\Game.exe'),'inert')
 $null=New-Item -ItemType Directory -Path (Join-Path $empty 'Fake.exe')
 Assert ((Get-D18GameDirectoryScope $empty).code -eq 'no_top_level_exe') 'nested EXE / directory named EXE insufficient'
 Assert ((Get-D18GameDirectoryScope (Join-Path $scratch 'missing')).code -eq 'invalid_path') 'missing directory'
 $file=Join-Path $scratch 'file';[IO.File]::WriteAllText($file,'file')
 Assert ((Get-D18GameDirectoryScope $file).code -eq 'invalid_path') 'file instead of directory'
 [IO.File]::WriteAllText((Join-Path $empty '.dlssnr-d18-install.json'),'{}')
 Assert ((Get-D18GameDirectoryScope $empty).code -eq 'no_top_level_exe') 'upgrade does not bypass scope for managed directory'
 $caught=$false
 try{$null=Assert-D18GameDirectoryScope $empty}catch{$caught=$_.Exception.Data['D18Scope'].code -eq 'no_top_level_exe'}
 Assert $caught 'central assertion carries structured bilingual reason'
 # Redirected Downloads must protect its real identity, not a guessed old default.
 $env:USERPROFILE=$fakeHome
 $redirected=Join-Path $scratch 'RedirectedDownloads'
 function Get-ItemProperty {
  [CmdletBinding()]param([string]$LiteralPath)
  return [pscustomobject]@{'{374DE290-123F-4565-9164-39C4925E467B}'=$redirected}
 }
 $identities=@(& $productionProtected)
 Assert ($identities -contains $redirected -and $identities -notcontains (Join-Path $fakeHome 'Downloads')) 'redirected Downloads identity replaces guessed default'
 Write-Output "$checks checks passed. Fixtures retained: $scratch"
} finally {$env:SystemRoot=$savedWindows;$env:USERPROFILE=$savedHome}
