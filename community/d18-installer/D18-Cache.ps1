#Requires -Version 5.1
# Each GUI session owns an isolated cache. Never infer ownership from file extensions.
function Assert-D18CachePath {
 param([string]$Root,[string]$Path)
 $rootFull=[IO.Path]::GetFullPath($Root).TrimEnd('\')
 $full=[IO.Path]::GetFullPath($Path).TrimEnd('\')
 if($full -ine $rootFull -and -not $full.StartsWith($rootFull+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cache path escapes its session.'}
 $cursor=$full
 while($cursor){
  if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Linked cache paths are not supported.'}
  $cursor=Split-Path -Parent $cursor
 }
 return $full
}
function Get-D18CacheInventory {
 param([string]$Root)
 $found=@{}
 foreach($f in @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force)){
  $rel=$f.FullName.Substring($Root.Length+1)
  if($rel -in @('.d18-owner.json','.d18-use.lock','.d18-files.json','.d18-protected-nr.json')){continue}
  $null=Assert-D18CachePath $Root $f.FullName
  $found[$rel]=@{path=$rel;bytes=$f.Length;sha256=(Get-D18Sha256 $f.FullName)}
 }
 return $found
}
function Protect-D18OriginalNr {
 param($Session,[string]$Path)
 if(-not $Path){return}
 $meta=Assert-D18CachePath $Session.root (Join-Path $Session.root '.d18-protected-nr.json')
 $paths=@()
 if(Test-Path -LiteralPath $meta){$paths=@((Get-Content -LiteralPath $meta -Raw|ConvertFrom-Json).paths)}
 $full=[IO.Path]::GetFullPath($Path)
 if($full -notin $paths){$paths+=$full}
 @{paths=$paths}|ConvertTo-Json|Set-Content -LiteralPath $meta -Encoding UTF8
}
function Open-D18CacheSession {
 param([string]$Base,[string]$Id,[string]$Game='')
 if($Id -notmatch '^[a-fA-F0-9]{32}$'){throw 'Invalid cache session ID.'}
 $root=[IO.Path]::GetFullPath((Join-Path $Base ('session-'+$Id))).TrimEnd('\')
 if($Game){$g=[IO.Path]::GetFullPath($Game).TrimEnd('\');if($root -ieq $g -or $root.StartsWith($g+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cache must be outside the game folder.'}}
 $null=Assert-D18CachePath $root $root
 $owner=Join-Path $root '.d18-owner.json'
 if(-not(Test-Path -LiteralPath $root)){
  $null=New-Item -ItemType Directory -Path $root
  @{schema='d18-session-cache-v1';id=$Id}|ConvertTo-Json|Set-Content -LiteralPath $owner -Encoding UTF8
 }
 if(-not(Test-Path -LiteralPath $owner)){throw 'Unowned cache directory; refusing to manage it.'}
 $meta=Get-Content -LiteralPath $owner -Raw|ConvertFrom-Json
 if($meta.schema -ne 'd18-session-cache-v1' -or $meta.id -cne $Id){throw 'Cache ownership mismatch.'}
 try {$lock=[IO.File]::Open((Join-Path $root '.d18-use.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
 catch {throw 'CACHE_IN_USE: another operation is using this session; retry when it finishes.'}
 try {
  $before=Get-D18CacheInventory $root
  return @{root=$root;id=$Id;lock=$lock;before=$before}
 } catch {$lock.Dispose();throw}
}
function Save-D18CacheInventory {
 param($Session)
 $path=Join-Path $Session.root '.d18-files.json';$owned=@{}
 if(Test-Path -LiteralPath $path){
  $old=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
  if($old.schema -ne 'd18-cache-files-v1' -or $old.id -cne $Session.id){throw 'Invalid cache ownership manifest.'}
  foreach($f in @($old.files)){$owned[$f.path]=@{path=$f.path;bytes=$f.bytes;sha256=$f.sha256}}
 }
 $after=Get-D18CacheInventory $Session.root
 foreach($key in $after.Keys){
  # Pre-existing/user-supplied files are never adopted. Changed tracked files are
  # adopted only when this locked worker changed them, not between operations.
  if(-not $Session.before.ContainsKey($key) -or ($owned.ContainsKey($key) -and $Session.before[$key].sha256 -ne $after[$key].sha256)){$owned[$key]=$after[$key]}
 }
 @{schema='d18-cache-files-v1';id=$Session.id;files=@($owned.Values)}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $path -Encoding UTF8
}
function New-D18CacheInstallTicket {
 param($Session,[string]$Game,[string]$Path)
 $statePath=Join-Path $Game '.dlssnr-d18-install.json'
 $state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
 if($state.format -ne 'dlssnr-d18-install-state-v1' -or $state.game_dir.TrimEnd('\') -ine $Game.TrimEnd('\')){throw 'Installation state identity mismatch.'}
 foreach($f in $state.files){
  $target=Assert-D18CachePath $Game (Join-Path $Game $f.target_relative)
  if((Get-D18Sha256 $target) -ine $f.installed_sha256){throw "Installed file changed before cache cleanup: $($f.target_relative)"}
 }
 @{schema='d18-cache-install-ticket-v1';session=$Session.id;cache=$Session.root;game=$Game;state_sha256=(Get-D18Sha256 $statePath);verified_files=@($state.files).Count}|ConvertTo-Json|Set-Content -LiteralPath $Path -Encoding UTF8
 return $Path
}
function Clear-D18InstalledCache {
 param($Session,[string]$Ticket,[string]$Game,[string]$Receipt)
 $t=Get-Content -LiteralPath $Ticket -Raw|ConvertFrom-Json
 if($t.schema -ne 'd18-cache-install-ticket-v1' -or $t.session -cne $Session.id -or $t.cache -ine $Session.root -or $t.game.TrimEnd('\') -ine $Game.TrimEnd('\')){throw 'Cleanup ticket does not match this installation/session.'}
 $statePath=Join-Path $Game '.dlssnr-d18-install.json'
 if((Get-D18Sha256 $statePath) -ine $t.state_sha256){throw 'Installation changed; cache retained.'}
 # Revalidate actual installed bytes, not just the earlier success flag.
 $state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
 foreach($f in $state.files){$p=Assert-D18CachePath $Game (Join-Path $Game $f.target_relative);if((Get-D18Sha256 $p) -ine $f.installed_sha256){throw 'Installed files changed; cache retained.'}}
 $manifest=Join-Path $Session.root '.d18-files.json'
 $inventory=Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json
 if($inventory.schema -ne 'd18-cache-files-v1' -or $inventory.id -cne $Session.id){throw 'Invalid cleanup manifest.'}
 $rows=New-Object System.Collections.Generic.List[object];$retained=New-Object System.Collections.Generic.List[object]
 $protected=@()
 $protectedPath=Assert-D18CachePath $Session.root (Join-Path $Session.root '.d18-protected-nr.json')
 if(Test-Path -LiteralPath $protectedPath){$protected=@((Get-Content -LiteralPath $protectedPath -Raw|ConvertFrom-Json).paths)}
 [long]$bytes=0
 foreach($f in @($inventory.files)){
  $status='retained';$reason=''
  try {
   if([IO.Path]::IsPathRooted($f.path) -or $f.path -match '(^|[\\/])\.\.([\\/]|$)' -or $f.path -in @('.d18-owner.json','.d18-use.lock','.d18-files.json','.d18-protected-nr.json')){throw 'Invalid cleanup target.'}
   $p=Assert-D18CachePath $Session.root (Join-Path $Session.root $f.path)
   if($p -in $protected){$reason='user_original_nr'}
   elseif(-not(Test-Path -LiteralPath $p -PathType Leaf)){$status='absent'}
   elseif((Get-D18Sha256 $p) -ine $f.sha256){$reason='hash_changed'}
   else{Remove-Item -LiteralPath $p -Force;$status='removed';$bytes+=[long]$f.bytes}
  } catch {$reason=$_.Exception.Message}
  if($status -eq 'retained'){$retained.Add($f)}
  $rows.Add(@{path=$f.path;sha256=$f.sha256;bytes=$f.bytes;status=$status;reason=$reason})
 }
 # Only empty directories inside the verified session are removed; never recurse-delete.
 foreach($d in @(Get-ChildItem -LiteralPath $Session.root -Directory -Recurse|Sort-Object FullName -Descending)){
  $null=Assert-D18CachePath $Session.root $d.FullName
  if(@(Get-ChildItem -LiteralPath $d.FullName -Force).Count -eq 0){Remove-Item -LiteralPath $d.FullName}
 }
 @{schema='d18-cache-files-v1';id=$Session.id;files=@($retained.ToArray())}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $manifest -Encoding UTF8
 $r=@{schema='d18-cache-cleanup-v1';status=$(if($retained.Count){'partial'}else{'cleaned'});cache=$Session.root;removed_bytes=$bytes;retained_files=$retained.Count;files=@($rows.ToArray());game_files_untouched=$true}
 $r|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $Receipt -Encoding UTF8
 return $r
}
