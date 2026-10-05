#Requires -Version 5.1
# UE bootstrap resolution retains the existing ReShade setup reference.
# Game grouping and primary selection below are independently implemented.
# Reference: https://github.com/crosire/reshade/tree/main/setup (BSD-3-Clause).
if(-not ('D18.Bootstrap' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
namespace D18 {
 public static class Bootstrap {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern IntPtr LoadLibraryExW(string p, IntPtr f, uint flags);
  [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr h);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindResourceW(IntPtr h, IntPtr name, IntPtr type);
  [DllImport("kernel32.dll")] static extern IntPtr LoadResource(IntPtr h, IntPtr r);
  [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr r);
  [DllImport("kernel32.dll")] static extern uint SizeofResource(IntPtr h, IntPtr r);
  public static string Resolve(string path) {
   path=Path.GetFullPath(path); IntPtr h=LoadLibraryExW(path,IntPtr.Zero,2);
   if(h==IntPtr.Zero) return path;
   try {
    IntPtr r=FindResourceW(h,(IntPtr)201,(IntPtr)10); if(r==IntPtr.Zero) return path;
    uint size=SizeofResource(h,r); if(size<2 || size>65536 || size%2!=0) return path;
    IntPtr ptr=LockResource(LoadResource(h,r)); if(ptr==IntPtr.Zero) return path;
    string value=Marshal.PtrToStringUni(ptr,(int)size/2).TrimEnd('\0');
    string target=Path.GetFullPath(Path.Combine(Path.GetDirectoryName(path),value));
    return File.Exists(target) && Path.GetExtension(target).Equals(".exe",StringComparison.OrdinalIgnoreCase) ? target:path;
   } catch {return path;} finally {FreeLibrary(h);}
  }
 }
}
'@
}
function ConvertTo-D18DiscoveryPath {
    param([string]$Path)
    try {
        if([string]::IsNullOrWhiteSpace($Path) -or -not [IO.Path]::IsPathRooted($Path)){return}
        $full=[IO.Path]::GetFullPath($Path)
        if($full -eq [IO.Path]::GetPathRoot($full)){return $full}
        $full.TrimEnd('\','/')
    } catch {}
}

function Test-D18DiscoveryChild {
    param([string]$Path,[string]$Root)
    $Path.Equals($Root,[StringComparison]::OrdinalIgnoreCase) -or
        $Path.StartsWith($Root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)
}

function Test-D18DiscoveryProduct {
    param([string]$Name)
    # Small explicit product denylist, not an attempted offline store taxonomy.
    $Name -notmatch '(?i)^(Steamworks Common Redistributables|SteamVR|3DMark(?:\s.*)?|PCMark(?:\s.*)?|DSX|DS4Windows|Wallpaper Engine)$'
}

function Test-D18DiscoveryDirectory {
    param($Item)
    $Item.PSIsContainer -and
        -not ($Item.Attributes -band ([IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::ReparsePoint)) -and
        $Item.Name -notmatch '(?i)^(windows|_CommonRedist|__Installer|cache|support|\.git|D18_Backups|jre.*|jdk.*|java|MelonLoader|EasyAntiCheat.*|BattlEye|artbook|digital.?art.?book|AdvGuide|Tools?|LocalData|webviewsupport.*|cc_bin|ccplayer|BlizzardBrowser|BlizzardError)$'
}

function Test-D18DiscoveryExecutable {
    param([string]$Path)
    $name=[IO.Path]::GetFileName($Path)
    [IO.Path]::GetExtension($Path) -ieq '.exe' -and
        $Path -notmatch '(?i)(^|\\)(jre[^\\]*|jdk[^\\]*|java|MelonLoader|EasyAntiCheat[^\\]*|BattlEye|_CommonRedist|__Installer|D18_Backups|AdvGuide|Tools?|LocalData|webviewsupport[^\\]*|cc_bin|ccplayer|BlizzardBrowser|BlizzardError)\\' -and
        $name -notmatch '(?i)(launcher|crash|unins|setup|updater|redist|report|helper|service|webview|subprocess|dummywindow|ClearThirdParty|KRSDKExternal|anticheat|anti.cheat|artbook|digital.?art.?book|MelonLoader)' -and
        $name -notmatch '(?i)^(crs[-_].*|BlizzardBrowser|BlizzardError|FenrisError|KRInstallExternal|hpatchz|D3D12StateObjectCompiler|d3dconfig|start_game_in_offline_mode|CCGameRecordApp64|CCMini|CCVideoPlayerApp64|MLiveCCPlayerApp64|get-graphics-offsets64)\.exe$' -and
        $name -notmatch '(?i)^(7z(a|g|z)?|unrar|winrar|java(w|ws)?|javac|javadoc|jar|jarsigner|jcmd|jconsole|jdb|jdeps|jfr|jinfo|jlink|jmap|jmod|jpackage|jps|jrunscript|jshell|jstack|jstat(d)?|keytool|pack200|unpack200|rmid|rmiregistry|serialver|tnameserv|orbd|servertool|createdump|UnityCrashHandler\d*|start_protected_game|toggle.*(eac|anti.*cheat))\.exe$'
}

function Get-D18DiscoveryText {
    param([string]$Path)
    # Metadata must not allocate an unbounded buffer in the in-process host.
    try {
        $file=Get-Item -LiteralPath $Path -ErrorAction Stop
        if($file.PSIsContainer -or $file.Length -gt 16MB){return}
        [IO.File]::ReadAllText($file.FullName,[Text.Encoding]::UTF8)
    } catch {}
}

function Get-D18NvidiaGames {
    param([string]$Path)
    try {$json=Get-D18DiscoveryText $Path | ConvertFrom-Json -ErrorAction Stop} catch {return}
    $seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in @($json.Applications)) {
        try {
            $app=$entry.Application
            if($app.DisplayName -isnot [string] -or [string]::IsNullOrWhiteSpace($app.DisplayName) -or
                $app.InstallDirectory -isnot [string] -or $app.IsCreativeApplication -isnot [bool] -or
                $app.IsCreativeApplication -or $app.DetectedFiles -isnot [array] -or $app.DetectedFiles.Count -eq 0){continue}
            if(-not (Test-D18DiscoveryProduct $app.DisplayName)){continue}
            $root=ConvertTo-D18DiscoveryPath $app.InstallDirectory
            if(-not $root -or $app.DetectedFiles[0] -isnot [string]){continue}
            $main=$app.DetectedFiles[0]
            if(-not [IO.Path]::IsPathRooted($main)){$main=Join-Path $root $main}
            $main=ConvertTo-D18DiscoveryPath $main
            if(-not $main -or -not (Test-D18DiscoveryChild $main $root) -or
                -not (Test-D18DiscoveryExecutable $main)){continue}
            $f=Get-Item -LiteralPath $main -ErrorAction Stop
            if($f.PSIsContainer -or ($f.Attributes -band [IO.FileAttributes]::ReparsePoint)){continue}
            if(-not $seen.Add($root)){continue}
            [pscustomobject]@{name=$app.DisplayName;root=$root;main=$main;source='nvidia'}
        } catch {continue}
    }
}

function Get-D18SteamGames {
    param([string[]]$SteamApps)
    foreach($apps in $SteamApps) {
        try {
            foreach($acf in @(Get-ChildItem -LiteralPath $apps -Filter 'appmanifest_*.acf' -File -ErrorAction SilentlyContinue)) {
                try {
                    $text=Get-D18DiscoveryText $acf.FullName
                    if(-not $text){continue}
                    $name=[regex]::Match($text,'(?m)^\s*"name"\s+"([^"\r\n]+)"').Groups[1].Value
                    $dir=[regex]::Match($text,'(?m)^\s*"installdir"\s+"([^"\r\n]+)"').Groups[1].Value
                    $id=[regex]::Match($text,'(?m)^\s*"appid"\s+"(\d+)"').Groups[1].Value
                    if(-not $name -or -not $dir -or $id -eq '228980' -or -not (Test-D18DiscoveryProduct $name)){continue}
                    $common=ConvertTo-D18DiscoveryPath (Join-Path $apps 'common')
                    $root=ConvertTo-D18DiscoveryPath (Join-Path $common $dir)
                    if(-not $root -or $root -ieq $common -or -not (Test-D18DiscoveryChild $root $common)){continue}
                    [pscustomobject]@{name=$name;root=$root;main='';source='steam'}
                } catch {continue}
            }
        } catch {continue}
    }
}

function Get-D18PlatformGames {
    # Separate source failures so a damaged manifest cannot empty other libraries.
    try {
        $steam=(Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
        if(-not $steam){$steam=(Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath}
        if($steam) {
            $apps=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
            $null=$apps.Add((Join-Path $steam 'steamapps'))
            foreach($file in @('steamapps\libraryfolders.vdf','config\libraryfolders.vdf')) {
                $vdfText=Get-D18DiscoveryText (Join-Path $steam $file)
                if(-not $vdfText){continue}
                foreach($m in [regex]::Matches($vdfText,'"path"\s+"([^"]+)"')) {
                    $null=$apps.Add((Join-Path ($m.Groups[1].Value.Replace('\\','\')) 'steamapps'))
                }
            }
            Get-D18SteamGames @($apps)
        }
    } catch {}
    try {
        $epic=Join-Path $env:ProgramData 'Epic\EpicGamesLauncher\Data\Manifests'
        foreach($item in @(Get-ChildItem -LiteralPath $epic -Filter '*.item' -File -ErrorAction SilentlyContinue)) {
            try {
                $m=Get-D18DiscoveryText $item.FullName | ConvertFrom-Json -ErrorAction Stop
                if($m.InstallLocation -is [string]) {
                    [pscustomobject]@{name=[string]$m.DisplayName;root=$m.InstallLocation;main='';source='epic'}
                }
            } catch {continue}
        }
    } catch {}
    try {
        foreach($key in @(Get-ChildItem 'HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games' -ErrorAction SilentlyContinue)) {
            try {
                $m=Get-ItemProperty $key.PSPath -ErrorAction Stop
                if($m.path){[pscustomobject]@{name=[string]$m.gameName;root=[string]$m.path;main='';source='gog'}}
            } catch {continue}
        }
    } catch {}
    try {
        $ea=Join-Path $env:LOCALAPPDATA 'Electronic Arts\EA Desktop'
        foreach($ini in @(Get-ChildItem -LiteralPath $ea -Filter 'user_*.ini' -File -ErrorAction SilentlyContinue)) {
            $iniText=Get-D18DiscoveryText $ini.FullName
            if(-not $iniText){continue}
            foreach($m in [regex]::Matches($iniText,'(?m)^user.downloadinplacedir\s*=\s*(.+)$')) {
                # EA's setting is a library directory, not a per-title manifest.
                Get-D18RootGames -Roots @($m.Groups[1].Value.Trim().Trim('"')) -Source 'ea'
            }
        }
    } catch {}
}

function Get-D18RootGames {
    param([string[]]$Roots,[string]$Source='manual')
    foreach($path in $Roots) {
        try {
            $root=ConvertTo-D18DiscoveryPath $path
            if(-not $root){continue}
            $item=Get-Item -LiteralPath $root -ErrorAction Stop
            if(-not (Test-D18DiscoveryDirectory $item)){continue}
            $children=@(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue)
            $dirs=@($children | Where-Object {Test-D18DiscoveryDirectory $_})
            # Root EXE / engine layout implies one title, even with several asset folders.
            $single=@($children | Where-Object {-not $_.PSIsContainer -and (Test-D18DiscoveryExecutable $_.FullName)}).Count -gt 0
            $single=$single -or @($dirs | Where-Object {$_.Name -match '(?i)^(Engine|Binaries|bin|bin32|bin64|Content|Game|Client|Retail)$'}).Count -gt 0
            if(-not $single -and $dirs.Count -gt 1) {
                foreach($dir in $dirs){[pscustomobject]@{name=$dir.Name;root=$dir.FullName;main='';source=$Source}}
            } else {[pscustomobject]@{name=$item.Name;root=$root;main='';source=$Source}}
        } catch {continue}
    }
}

function Get-D18DiscoveryExecutables {
    param([string]$Root,[hashtable]$Budget)
    $queue=New-Object 'System.Collections.Generic.Queue[string]'
    $queue.Enqueue($Root)
    $seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    while($queue.Count -and $Budget.directories -lt 20000) {
        $dir=$queue.Dequeue()
        $Budget.directories++
        try {
            $item=Get-Item -LiteralPath $dir -ErrorAction Stop
            if(-not (Test-D18DiscoveryDirectory $item)){continue}
            foreach($f in @(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue)) {
                if($f.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
                if($f.PSIsContainer) {
                    if((Test-D18DiscoveryDirectory $f) -and $Budget.directories+$queue.Count -lt 20000){$queue.Enqueue($f.FullName)}
                    continue
                }
                if($f.Extension -ine '.exe'){continue}
                try {
                    # A UE bootstrap may be named Launcher; only admit it if it resolves.
                    if(-not (Test-D18DiscoveryExecutable $f.FullName) -and $f.Name -notmatch '(?i)launcher'){continue}
                    $resolved=[D18.Bootstrap]::Resolve($f.FullName)
                    if(-not (Test-D18DiscoveryChild $resolved $Root) -or -not (Test-D18DiscoveryExecutable $resolved)){continue}
                    $target=Get-Item -LiteralPath $resolved -ErrorAction Stop
                    if($target.PSIsContainer -or ($target.Attributes -band [IO.FileAttributes]::ReparsePoint) -or -not $seen.Add($target.FullName)){continue}
                    [pscustomobject]@{path=$target.FullName;selected=$f.FullName;size=$target.Length;shipping=($target.Name -like '*-Win64-Shipping.exe')}
                } catch {continue}
            }
        } catch {continue}
    }
}

function Get-D18GameCandidates {
    param([string[]]$Roots=@(),[int]$Limit=500)
    Set-StrictMode -Off
    if($Limit -le 0){return}
    $nvPath=if($env:LOCALAPPDATA){Join-Path $env:LOCALAPPDATA 'NVIDIA Corporation\NVIDIA app\NvBackend\ApplicationStorage.json'}else{''}
    $nvidia=@(Get-D18NvidiaGames $nvPath)
    $games=if($Roots.Count){@(Get-D18RootGames $Roots)}else{@(Get-D18PlatformGames)}
    $groups=New-Object 'System.Collections.Generic.List[object]'
    $rootsSeen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach($game in $games) {
        try {
            $root=ConvertTo-D18DiscoveryPath $game.root
            if(-not $root -or -not (Test-D18DiscoveryProduct $game.name) -or -not $rootsSeen.Add($root)){continue}
            $game.root=$root
            if([string]::IsNullOrWhiteSpace($game.name)){$game.name=[IO.Path]::GetFileName($root)}
            $groups.Add($game)
        } catch {continue}
    }
    # Match contained NVIDIA directories too (some point directly at Binaries/Win64).
    $matched=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach($game in $groups) {
        $nv=@($nvidia | Where-Object {Test-D18DiscoveryChild $_.root $game.root})
        if($nv.Count){
            $game.main=$nv[0].main;$game.name=$nv[0].name;$game.source='nvidia'
            foreach($entry in $nv){$null=$matched.Add($entry.root)}
        }
    }
    if(-not $Roots.Count) {
        foreach($nv in $nvidia) {
            if(-not $matched.Contains($nv.root) -and $rootsSeen.Add($nv.root)){$groups.Add($nv)}
        }
    }
    $budget=@{directories=0}
    $seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $count=0
    foreach($game in @($groups | Sort-Object @{Expression={$_.source -ne 'nvidia'}},root)) {
        if($count -ge $Limit -or $budget.directories -ge 20000){break}
        try {
            $files=@(Get-D18DiscoveryExecutables $game.root $budget | Sort-Object @{Expression='shipping';Descending=$true},@{Expression='size';Descending=$true},path)
            if(-not $files.Count){continue}
            $chosen=$files[0]
            if($game.main) {
                $resolved=[D18.Bootstrap]::Resolve($game.main)
                if(-not (Test-D18DiscoveryExecutable $resolved) -or -not (Test-D18DiscoveryChild $resolved $game.root)){continue}
                $target=Get-Item -LiteralPath $resolved -ErrorAction Stop
                if($target.PSIsContainer -or ($target.Attributes -band [IO.FileAttributes]::ReparsePoint)){continue}
                $chosen=[pscustomobject]@{path=$target.FullName;selected=$game.main}
            }
            if(-not $seen.Add($chosen.path)){continue}
            [pscustomobject]@{name=$game.name;path=$chosen.path;selected=$chosen.selected;source=$game.source;
                alternatives=[string[]]@($files | Where-Object {$_.path -ine $chosen.path} | ForEach-Object {$_.path})}
            $count++
        } catch {continue}
    }
}
