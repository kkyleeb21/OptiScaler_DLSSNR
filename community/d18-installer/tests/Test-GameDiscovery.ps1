#Requires -Version 5.1
[CmdletBinding()]
param([string]$ScratchRoot=[IO.Path]::GetTempPath())
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\D18-GameDiscovery.ps1')
$scratch=Join-Path ([IO.Path]::GetFullPath($ScratchRoot)) ('D18-scan-tests-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $scratch
$savedLocal=$env:LOCALAPPDATA
$savedData=$env:ProgramData
$checks=0
function Assert-Scan([bool]$Condition,[string]$Message) {
    if(-not $Condition){throw "FAIL $Message"}
    $script:checks++
    Write-Output "PASS $Message"
}
function New-FakeExe([string]$Relative,[int]$Size=32) {
    $path=Join-Path $scratch $Relative
    $null=New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
    $stream=[IO.File]::Open($path,[IO.FileMode]::Create,[IO.FileAccess]::Write)
    try {$stream.SetLength($Size)} finally {$stream.Dispose()}
    return $path
}
function Write-Fixture([string]$Path,[string]$Text) {
    $null=New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force
    [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
}
function New-SteamManifest([string]$Apps,[string]$Id,[string]$Name,[string]$Dir) {
    $text='"AppState"'+"`n{`n"+'"appid" "'+$Id+'"'+"`n"+'"name" "'+$Name+'"'+"`n"+'"installdir" "'+$Dir+'"'+"`n}"
    Write-Fixture (Join-Path $Apps ('appmanifest_'+$Id+'.acf')) $text
}

# Only registry reads are replaced; the production file/manifest collectors run unchanged.
function Get-ItemProperty {
    [CmdletBinding()]
    param([string]$Path)
    if($Path -eq 'HKCU:\Software\Valve\Steam'){return [pscustomobject]@{SteamPath=$script:fakeSteam}}
    if($Path -eq 'fixture-gog'){return [pscustomobject]@{path=$script:fakeGog;gameName='GOG Fixture'}}
    throw 'Synthetic inaccessible registry key'
}
function Get-ChildItem {
    [CmdletBinding()]
    param([string]$Path,[string]$LiteralPath,[string]$Filter,[switch]$Force,[switch]$File)
    if($Path -eq 'HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games') {
        return [pscustomobject]@{PSPath='fixture-gog'}
    }
    $argsForItem=@{Force=$Force;File=$File;ErrorAction='SilentlyContinue'}
    if($LiteralPath){$argsForItem.LiteralPath=$LiteralPath}else{$argsForItem.Path=$Path}
    if($Filter){$argsForItem.Filter=$Filter}
    Microsoft.PowerShell.Management\Get-ChildItem @argsForItem
}

try {
    $env:LOCALAPPDATA=Join-Path $scratch 'local'
    $env:ProgramData=Join-Path $scratch 'data'
    $script:fakeSteam=Join-Path $scratch 'Steam'
    $apps=Join-Path $fakeSteam 'steamapps'
    $secondApps=Join-Path $scratch 'SecondLibrary\steamapps'
    $script:fakeGog=Join-Path $scratch 'GOG\Fixture'
    $unicodeName='NVIDIA '+[char]0x6E38+[char]0x620F
    $nvMain=New-FakeExe 'Steam\steamapps\common\NvGame\bin\actual.exe' 40
    $nvOther=New-FakeExe 'Steam\steamapps\common\NvGame\bin\larger.exe' 400
    $shipping=New-FakeExe 'Steam\steamapps\common\UEGame\Project\Binaries\Win64\Project-Win64-Shipping.exe' 60
    $ueOther=New-FakeExe 'Steam\steamapps\common\UEGame\Project.exe' 600
    $largest=New-FakeExe 'Steam\steamapps\common\SizeGame\Game.exe' 200
    $small=New-FakeExe 'Steam\steamapps\common\SizeGame\GameDX11.exe' 100
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\CrashReport.exe' 900
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\7za.exe' 950
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\jre\bin\java.exe' 990
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\jre\bin\unrecognized.exe' 999
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\MelonLoader\SomeTool.exe' 1000
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\toggle_anti_cheat.exe' 1001
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\BlizzardError\FenrisError.exe' 1002
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\crs-handler.exe' 1003
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\crs-uploader.exe' 1004
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\BlizzardBrowser\BlizzardBrowser.exe' 1005
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\AdvGuide\Adventure Guide.exe' 1006
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\Tool\InstallerMessage.exe' 1007
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\LocalData\Patch\Game.exe' 1008
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\hpatchz.exe' 1009
    $null=New-FakeExe 'Steam\steamapps\common\SizeGame\d3dconfig.exe' 1010
    $null=New-FakeExe 'Steam\steamapps\common\Orphan\Orphan.exe'
    $null=New-FakeExe 'Steam\steamapps\common\EmptyGame\7za.exe'
    $null=New-FakeExe 'Steam\steamapps\common\3DMark\3DMark.exe'
    $null=New-FakeExe 'Steam\steamapps\common\DSX\DSX.exe'
    $null=New-FakeExe 'Steam\steamapps\common\Redist\Redistributable.exe'
    $second=New-FakeExe 'SecondLibrary\steamapps\common\NvGame\Game.exe'
    $epic=New-FakeExe 'Epic\Fixture\EpicGame.exe'
    $gog=New-FakeExe 'GOG\Fixture\GogGame.exe'
    $ea1=New-FakeExe 'EA\First\First.exe'
    $ea2=New-FakeExe 'EA\Second\Second.exe'
    $creative=New-FakeExe 'Creative\Discord.exe'
    $standalone=New-FakeExe 'Standalone\Standalone.exe'
    New-SteamManifest $apps '1' 'Manifest Nv Name' 'NvGame'
    New-SteamManifest $apps '2' 'UE Title' 'UEGame'
    New-SteamManifest $apps '3' 'Size Title' 'SizeGame'
    New-SteamManifest $apps '4' 'Empty Title' 'EmptyGame'
    New-SteamManifest $apps '223850' '3DMark' '3DMark'
    New-SteamManifest $apps '1812620' 'DSX' 'DSX'
    New-SteamManifest $apps '228980' 'Steamworks Common Redistributables' 'Redist'
    New-SteamManifest $apps '5' 'Missing Directory' 'Missing'
    New-SteamManifest $apps '6' 'Escaping Manifest' '..\..\Standalone'
    New-SteamManifest $secondApps '1' 'Same Title Second Install' 'NvGame'
    Write-Fixture (Join-Path $apps 'appmanifest_7.acf') 'broken manifest'
    $vdf='"libraryfolders"'+"`n{`n"+'"path" "'+(Split-Path -Parent $secondApps).Replace('\','\\')+'"'+"`n}"
    Write-Fixture (Join-Path $apps 'libraryfolders.vdf') $vdf
    $epicManifests=Join-Path $env:ProgramData 'Epic\EpicGamesLauncher\Data\Manifests'
    Write-Fixture (Join-Path $epicManifests 'good.item') (@{DisplayName='Epic Fixture';InstallLocation=(Split-Path -Parent $epic)}|ConvertTo-Json)
    Write-Fixture (Join-Path $epicManifests 'broken.item') '{broken'
    Write-Fixture (Join-Path $epicManifests 'duplicate.item') (@{DisplayName='Other Platform Same Path';InstallLocation=(Split-Path -Parent (Split-Path -Parent $nvMain))}|ConvertTo-Json)
    Write-Fixture (Join-Path $env:LOCALAPPDATA 'Electronic Arts\EA Desktop\user_fake.ini') ('user.downloadinplacedir = '+(Join-Path $scratch 'EA'))
    $nvPath=Join-Path $env:LOCALAPPDATA 'NVIDIA Corporation\NVIDIA app\NvBackend\ApplicationStorage.json'
    $nvJson=@{Applications=@(
        @{Application=@{DisplayName=$unicodeName;InstallDirectory=(Split-Path -Parent $nvMain);DetectedFiles=@($nvMain);IsCreativeApplication=$false}},
        @{Application=@{DisplayName='Duplicate';InstallDirectory=(Split-Path -Parent $nvMain).ToUpperInvariant();DetectedFiles=@($nvMain.ToUpperInvariant());IsCreativeApplication=$false}},
        @{Application=@{DisplayName='Standalone Title';InstallDirectory=(Split-Path -Parent $standalone);DetectedFiles=@($standalone);IsCreativeApplication=$false}},
        @{Application=@{DisplayName='Missing Main';InstallDirectory=(Split-Path -Parent $standalone);DetectedFiles=@((Join-Path $scratch 'missing.exe'),$standalone);IsCreativeApplication=$false}},
        @{Application=@{DisplayName='Discord';InstallDirectory=(Split-Path -Parent $creative);DetectedFiles=@($creative);IsCreativeApplication=$true}},
        @{Application=@{DisplayName='3DMark';InstallDirectory=(Join-Path $apps 'common\3DMark');DetectedFiles=@((Join-Path $apps 'common\3DMark\3DMark.exe'));IsCreativeApplication=$false}},
        @{Application=@{DisplayName='Missing Fields'}},
        @{Application=@{DisplayName='Wrong Types';InstallDirectory=(Split-Path -Parent $creative);DetectedFiles=$creative;IsCreativeApplication='false'}}
    )}
    Write-Fixture $nvPath ($nvJson|ConvertTo-Json -Depth 7)
    $before=@(Microsoft.PowerShell.Management\Get-ChildItem -LiteralPath $scratch -Recurse -File | Get-FileHash -Algorithm SHA256)
    $rows=@(Get-D18GameCandidates)
    Assert-Scan ($rows.Count -eq 9) 'one row per installed game (all five sources, two installs of one title)'
    $nv=@($rows|Where-Object {$_.path -ieq $nvMain})
    Assert-Scan ($nv.Count -eq 1 -and $nv[0].source -eq 'nvidia' -and $nv[0].name -ceq $unicodeName) 'NVIDIA first, nested install directory, UTF-8 name, case-insensitive dedup'
    Assert-Scan ($nv[0].selected -ieq $nvMain -and $nv[0].alternatives.Count -eq 1 -and $nv[0].alternatives[0] -ieq $nvOther) 'NVIDIA main beats larger EXE; alternatives exclude chosen path'
    $ue=@($rows|Where-Object {$_.name -eq 'UE Title'})
    Assert-Scan ($ue.Count -eq 1 -and $ue[0].path -ieq $shipping -and $ue[0].alternatives[0] -ieq $ueOther) 'UE shipping beats larger bootstrap candidate'
    $size=@($rows|Where-Object {$_.name -eq 'Size Title'})
    Assert-Scan ($size.Count -eq 1 -and $size[0].path -ieq $largest -and $size[0].alternatives.Count -eq 1 -and $size[0].alternatives[0] -ieq $small) 'largest eligible EXE; crash, compression, JRE, mods and anti-cheat excluded'
    Assert-Scan (@($rows|Where-Object {$_.path -ieq $second}).Count -eq 1) 'Steam libraryfolders finds independent second installation'
    Assert-Scan (@($rows|Where-Object {$_.source -eq 'epic' -and $_.name -eq 'Epic Fixture'}).Count -eq 1) 'Epic manifest and display name'
    Assert-Scan (@($rows|Where-Object {$_.source -eq 'gog' -and $_.name -eq 'GOG Fixture'}).Count -eq 1) 'GOG registry path and gameName'
    Assert-Scan (@($rows|Where-Object {$_.source -eq 'ea'}).Count -eq 2) 'EA setting split into individual installed games'
    Assert-Scan (@($rows|Where-Object {$_.name -match 'Missing|Discord|3DMark|DSX|Redist|Orphan|Empty|Escaping|Wrong Types'}).Count -eq 0) 'stale/creative/non-game/malformed/orphan/empty entries absent'
    Assert-Scan (@($rows|Where-Object {$_.source -eq 'nvidia'})[0].source -eq $rows[0].source) 'NVIDIA rows precede fallback rows'
    Assert-Scan (@($rows|Where-Object {$_.alternatives -isnot [string[]]}).Count -eq 0) 'alternatives always string arrays including empty arrays'
    $manual=Join-Path $scratch 'Manual'
    $manualOne=New-FakeExe 'Manual\First\First.exe'
    $manualTwo=New-FakeExe 'Manual\Second\Second.exe'
    $manualRows=@(Get-D18GameCandidates -Roots @($manual,$manual.ToUpperInvariant()))
    Assert-Scan ($manualRows.Count -eq 2 -and @($manualRows|Where-Object {$_.source -ne 'manual'}).Count -eq 0) 'manual multi-game root split and overlapping roots dedup'
    $single=@(Get-D18GameCandidates -Roots @((Join-Path $apps 'common\UEGame')))
    Assert-Scan ($single.Count -eq 1 -and $single[0].path -ieq $shipping) 'manual single UE root remains one game'
    $scoped=@(Get-D18GameCandidates -Roots @((Join-Path $apps 'common\NvGame')))
    Assert-Scan ($scoped.Count -eq 1 -and $scoped[0].name -ceq $unicodeName) 'manual scan uses matching NVIDIA without leaking unrelated games'
    Assert-Scan (@(Get-D18GameCandidates -Limit 2).Count -eq 2) 'result limit honored'
    Assert-Scan (@(Get-D18GameCandidates -Limit 0).Count -eq 0) 'zero result limit'
    # Exercise the hard directory ceiling without manufacturing 20,000 folders.
    $budget=@{directories=19999}
    $bounded=@(Get-D18DiscoveryExecutables (Join-Path $apps 'common\UEGame') $budget)
    Assert-Scan ($budget.directories -eq 20000 -and $bounded.Count -eq 1) 'directory budget stops before descending'
    $after=@(Microsoft.PowerShell.Management\Get-ChildItem -LiteralPath $scratch -Recurse -File | Get-FileHash -Algorithm SHA256)
    $same=@($before|Where-Object {$b=$_; @($after|Where-Object {$_.Path -eq $b.Path -and $_.Hash -eq $b.Hash}).Count -eq 1})
    Assert-Scan ($same.Count -eq $before.Count) 'discovery preserves every original fixture byte'
    Write-Fixture $nvPath '{damaged JSON'
    $fallback=@(Get-D18GameCandidates)
    Assert-Scan ($fallback.Count -eq 8 -and @($fallback|Where-Object {$_.source -eq 'nvidia'}).Count -eq 0) 'damaged NVIDIA JSON silently falls back to platforms'
    Write-Fixture $nvPath '{"UnexpectedSchema":true}'
    Assert-Scan (@(Get-D18GameCandidates).Count -eq 8) 'changed NVIDIA schema silently falls back'
    # Rename only a test-owned file; scan sees a genuinely absent NVIDIA file.
    Move-Item -LiteralPath $nvPath -Destination ($nvPath+'.fixture')
    Assert-Scan (@(Get-D18GameCandidates).Count -eq 8) 'missing NVIDIA JSON silently falls back'
    Assert-Scan (@(Get-D18GameCandidates -Roots @((Join-Path $scratch 'absent'),'invalid:bad')).Count -eq 0) 'bad manual roots do not throw'
    Write-Output "$checks checks passed. Fixtures retained: $scratch"
} finally {
    $env:LOCALAPPDATA=$savedLocal
    $env:ProgramData=$savedData
}
