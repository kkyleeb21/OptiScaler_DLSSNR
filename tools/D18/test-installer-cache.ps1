#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$Installer,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
. (Join-Path $Installer 'D18-Common.ps1')
. (Join-Path $Installer 'D18-Cache.ps1')
Set-StrictMode -Off
$root=Join-Path $Output ([guid]::NewGuid().ToString('N'));$null=New-Item -ItemType Directory -Path $root -Force
$game=Join-Path $root 'game';$null=New-Item -ItemType Directory -Path $game
$product=Join-Path $game 'installed.dll';[IO.File]::WriteAllText($product,'installed bytes')
$state=@{format='dlssnr-d18-install-state-v1';game_dir=$game;files=@(@{target_relative='installed.dll';installed_sha256=(Get-D18Sha256 $product)})}
$state|ConvertTo-Json -Depth 5|Set-Content (Join-Path $game '.dlssnr-d18-install.json') -Encoding UTF8
$original=Join-Path $root 'user-nr.dll';[IO.File]::WriteAllText($original,'original NR');$originalHash=Get-D18Sha256 $original
$checks=New-Object System.Collections.Generic.List[string]
function Check($ok,$label){if(-not $ok){throw "FAIL: $label"};$checks.Add($label)}
$base=Join-Path $root 'cache';$id=[guid]::NewGuid().ToString('N');$s=Open-D18CacheSession $base $id $game
try {
 $caught=$false;try{$other=Open-D18CacheSession $base $id $game;$other.lock.Dispose()}catch{$caught=$_.Exception.Message -like 'CACHE_IN_USE*'}
 Check $caught 'active session lock blocks overlapping cleanup/worker'
 foreach($n in @('download.zip','temporary-nr.dll','user-selected-nr.dll','changed.dll')){[IO.File]::WriteAllText((Join-Path $s.root $n),$n)}
 Protect-D18OriginalNr $s $original;Protect-D18OriginalNr $s (Join-Path $s.root 'user-selected-nr.dll')
 Save-D18CacheInventory $s
 $ticket=New-D18CacheInstallTicket $s $game (Join-Path $root 'ticket.json')
} finally {$s.lock.Dispose()}
$scope=$s.root
[IO.File]::WriteAllText((Join-Path $scope 'changed.dll'),'user modified after preparation')
[IO.File]::WriteAllText((Join-Path $scope 'unknown.txt'),'unowned file')
$other=Open-D18CacheSession $base ([guid]::NewGuid().ToString('N')) $game
[IO.File]::WriteAllText((Join-Path $other.root 'other.zip'),'other active installer')
$s=Open-D18CacheSession $base $id $game
try {
 Save-D18CacheInventory $s
 $caught=$false;try{Clear-D18InstalledCache $s (Join-Path $root 'nonexistent-ticket.json') $game (Join-Path $root 'bad.json')}catch{$caught=$true}
 Check ($caught -and (Test-Path (Join-Path $scope 'download.zip'))) 'no successful-install ticket means no deletion'
 [IO.File]::WriteAllText($product,'changed game')
 $caught=$false;try{Clear-D18InstalledCache $s $ticket $game (Join-Path $root 'bad.json')}catch{$caught=$true}
 Check ($caught -and (Test-Path (Join-Path $scope 'temporary-nr.dll'))) 'changed installed bytes retain cache'
 [IO.File]::WriteAllText($product,'installed bytes')
 $result=Clear-D18InstalledCache $s $ticket $game (Join-Path $root 'receipt.json')
 Check (-not(Test-Path (Join-Path $scope 'download.zip')) -and -not(Test-Path (Join-Path $scope 'temporary-nr.dll'))) 'verified installation clears owned ZIP and generated NR copy'
 Check ((Get-D18Sha256 $original) -eq $originalHash -and (Test-Path (Join-Path $scope 'user-selected-nr.dll'))) 'original NR protected outside and inside cache'
 Check (Test-Path (Join-Path $scope 'changed.dll')) 'changed cached bytes retained'
 Check (Test-Path (Join-Path $scope 'unknown.txt')) 'unowned cache files retained'
 Check (Test-Path (Join-Path $other.root 'other.zip')) 'other active installer session untouched'
 Check ((Get-D18Sha256 $product) -eq $state.files[0].installed_sha256) 'game files untouched'
 Check ($result.status -eq 'partial' -and $result.retained_files -eq 2) 'receipt reports retained exceptions'
 # A malformed recorded target must never escape the scoped cache.
 $m=Get-Content (Join-Path $scope '.d18-files.json') -Raw|ConvertFrom-Json
 $m.files+=@{path='..\..\user-nr.dll';bytes=11;sha256=$originalHash}
 $m|ConvertTo-Json -Depth 5|Set-Content (Join-Path $scope '.d18-files.json') -Encoding UTF8
 $r=Clear-D18InstalledCache $s $ticket $game (Join-Path $root 'escape-receipt.json')
 Check ((Get-D18Sha256 $original) -eq $originalHash -and @($r.files|Where-Object reason -eq 'Invalid cleanup target.').Count -eq 1) 'path traversal rejected without deleting original'
} finally {$s.lock.Dispose();$other.lock.Dispose()}
$r=@{pass=$true;scope='filesystem lifecycle fixture; no real game mutation';checks=@($checks.ToArray());root=$root}
$r|ConvertTo-Json -Depth 5|Set-Content (Join-Path $Output 'result.json') -Encoding UTF8
$r|ConvertTo-Json -Depth 5
