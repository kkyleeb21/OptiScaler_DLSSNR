$ErrorActionPreference = 'Stop'
$source = 'E:\DLSSNR\builds\D18CommonUiDiagnostics-20260906\OptiScaler.dll'
$expected = '606A7470B9DD30160BDE8FA726D64561069F32C3D9735BA7A7A66E624BDE23DA'
$old = 'C3E8F20F5AD48248E78B3B847DB25463C4214D0A81A99C1022452D420CE1A507'
if ((Get-FileHash -LiteralPath $source).Hash -ne $expected) { throw 'Source mismatch' }
$games = @(
    @{name='Cyberpunk'; path='D:\SteamLibrary\steamapps\common\Cyberpunk 2077\bin\x64'; process='Cyberpunk2077.exe'},
    @{name='EldenRing'; path='D:\SteamLibrary\steamapps\common\ELDEN RING\Game'; process='eldenring.exe'}
)
foreach ($game in $games) {
    $target = Join-Path $game.path 'dxgi.dll'
    function Assert-Exited {
        if (@(Get-CimInstance Win32_Process | Where-Object { $_.Name -eq $game.process -or $_.ExecutablePath -like ($game.path + '\*') }).Count) {
            throw ($game.name + ' is running')
        }
    }
    Assert-Exited
    if ((Get-FileHash -LiteralPath $target).Hash -ne $old) { throw 'Installed core changed' }
    $preserve = @{}
    foreach ($name in @('OptiScaler.ini','nvngx.dll_dlssnr.dll','nvngx_dlssnr.dll')) {
        $preserve[$name] = (Get-FileHash -LiteralPath (Join-Path $game.path $name)).Hash
    }
    $backup = 'E:\DLSSNR\backups\' + $game.name + '-before-D18-0.1.2-' + (Get-Date -Format 'yyyyMMdd_HHmmss')
    New-Item -ItemType Directory -Path $backup | Out-Null
    foreach ($name in @('dxgi.dll','OptiScaler.ini','nvngx.dll_dlssnr.dll')) {
        Copy-Item -LiteralPath (Join-Path $game.path $name) -Destination $backup
        if ((Get-FileHash -LiteralPath (Join-Path $backup $name)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $game.path $name)).Hash) { throw 'Backup mismatch' }
    }
    Assert-Exited
    try {
        Copy-Item -LiteralPath $source -Destination $target -Force
        if ((Get-FileHash -LiteralPath $target).Hash -ne $expected) { throw 'Deploy mismatch' }
        foreach ($name in $preserve.Keys) {
            if ((Get-FileHash -LiteralPath (Join-Path $game.path $name)).Hash -ne $preserve[$name]) { throw "Preserved file changed: $name" }
        }
    } catch {
        Copy-Item -LiteralPath "$backup\dxgi.dll" -Destination $target -Force
        throw
    }
    $record = @{target=$target; backup=$backup; old_sha256=$old; new_sha256=$expected; preserved=$preserve; version='0.1.2'; game_tested=$false}
    [IO.File]::WriteAllText("$backup\deployment.json", ($record | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
    $record | ConvertTo-Json -Depth 5
}
