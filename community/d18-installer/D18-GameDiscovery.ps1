#Requires -Version 5.1
# Discovery follows ReShade setup's library scan / UE bootstrap flow.
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
function Get-D18GameCandidates {
    param([string[]]$Roots=@(),[int]$Limit=500)
    $paths=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach($p in $Roots){if(Test-Path -LiteralPath $p -PathType Container){$null=$paths.Add($p)}}
    if($Roots.Count -eq 0){
        $steam=(Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
        if($steam){
            $null=$paths.Add((Join-Path $steam 'steamapps\common'))
            foreach($file in @('steamapps\libraryfolders.vdf','config\libraryfolders.vdf')){
                $vdf=Join-Path $steam $file
                if(Test-Path -LiteralPath $vdf){foreach($m in [regex]::Matches([IO.File]::ReadAllText($vdf),'"path"\s+"([^"]+)"')){$null=$paths.Add((Join-Path ($m.Groups[1].Value.Replace('\\','\')) 'steamapps\common'))}}
            }
        }
        $epic=Join-Path $env:ProgramData 'Epic\EpicGamesLauncher\Data\Manifests'
        foreach($item in @(Get-ChildItem -LiteralPath $epic -Filter '*.item' -ErrorAction SilentlyContinue)){
            try{$m=Get-Content -LiteralPath $item.FullName -Raw | ConvertFrom-Json; if($m.InstallLocation){$null=$paths.Add([string]$m.InstallLocation)}}catch{}
        }
        foreach($key in @(Get-ChildItem 'HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games' -ErrorAction SilentlyContinue)){
            $p=(Get-ItemProperty $key.PSPath -ErrorAction SilentlyContinue).path; if($p){$null=$paths.Add($p)}
        }
        $ea=Join-Path $env:LOCALAPPDATA 'Electronic Arts\EA Desktop'
        foreach($ini in @(Get-ChildItem -LiteralPath $ea -Filter 'user_*.ini' -ErrorAction SilentlyContinue)){
            foreach($m in [regex]::Matches([IO.File]::ReadAllText($ini.FullName),'(?m)^user.downloadinplacedir\s*=\s*(.+)$')){$null=$paths.Add($m.Groups[1].Value.Trim())}
        }
    }
    $queue=New-Object 'System.Collections.Generic.Queue[string]'; foreach($p in $paths){if(Test-Path -LiteralPath $p -PathType Container){$queue.Enqueue($p)}}
    $seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $count=0; $directories=0
    while($queue.Count -and $count -lt $Limit -and $directories -lt 20000){
        $dir=$queue.Dequeue(); $directories++
        foreach($f in @(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue)){
            if($f.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
            if($f.PSIsContainer){if($f.Name -notmatch '(?i)^(windows|_CommonRedist|__Installer|cache|support|\.git|D18_Backups)$' -and -not($f.Attributes -band [IO.FileAttributes]::Hidden)){$queue.Enqueue($f.FullName)};continue}
            if($f.Extension -ine '.exe' -or $f.Name -match '(?i)(launcher|crash|unins|setup|updater|redist|report|helper|service|webview|subprocess|dummywindow|ClearThirdParty|KRSDKExternal)'){continue}
            $resolved=[D18.Bootstrap]::Resolve($f.FullName)
            if(-not $seen.Add($resolved)){continue}
            $name=[Diagnostics.FileVersionInfo]::GetVersionInfo($resolved).FileDescription
            if(-not $name){$name=[IO.Path]::GetFileNameWithoutExtension($resolved)}
            [pscustomobject]@{name=$name;path=$resolved;selected=$f.FullName}
            $count++; if($count -ge $Limit){break}
        }
    }
}
