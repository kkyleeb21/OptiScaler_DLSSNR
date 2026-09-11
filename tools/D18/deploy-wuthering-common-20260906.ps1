$ErrorActionPreference = 'Stop'
$game = 'D:\SteamLibrary\steamapps\common\Wuthering Waves\Client\Binaries\Win64'
$source = 'E:\DLSSNR\builds\D18CommonUiDiagnostics-20260906\OptiScaler.dll'
$expected = '606A7470B9DD30160BDE8FA726D64561069F32C3D9735BA7A7A66E624BDE23DA'
$oldExpected = 'C3E8F20F5AD48248E78B3B847DB25463C4214D0A81A99C1022452D420CE1A507'
function Assert-GameExited {
    $running = @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
        $_.Name -match 'Wuthering|Client-Win64|Client-Win' -or
        $_.ExecutablePath -like 'D:\SteamLibrary\steamapps\common\Wuthering Waves\*'
    })
    if ($running.Count) { throw ('Game/launcher is running: ' + ($running.Name -join ', ')) }
}
Assert-GameExited
if ((Get-FileHash -LiteralPath $source).Hash -ne $expected) { throw 'Candidate hash mismatch' }
$target = Join-Path $game 'dxgi.dll'
if ((Get-FileHash -LiteralPath $target).Hash -ne $oldExpected) { throw 'Installed core changed since preflight' }
$preserve = @('OptiScaler.ini','nvngx.dll_dlssnr.dll','nvngx_dlssnr.dll')
$before = @{}
foreach ($name in $preserve) { $before[$name] = (Get-FileHash -LiteralPath (Join-Path $game $name)).Hash }
$backup = Join-Path 'E:\DLSSNR\backups' ('Wuthering-before-D18CommonUiDiagnostics-' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
if (Test-Path -LiteralPath $backup) { throw 'Backup path already exists' }
New-Item -ItemType Directory -Path $backup | Out-Null
foreach ($name in @('dxgi.dll','OptiScaler.ini','nvngx.dll_dlssnr.dll','OptiScaler.log')) {
    $item = Join-Path $game $name
    if (Test-Path -LiteralPath $item -PathType Leaf) { Copy-Item -LiteralPath $item -Destination (Join-Path $backup $name) }
}
if ((Get-FileHash -LiteralPath (Join-Path $backup 'dxgi.dll')).Hash -ne $oldExpected) { throw 'Backup verification failed' }
Assert-GameExited
try {
    Copy-Item -LiteralPath $source -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target).Hash -ne $expected) { throw 'Deployed hash mismatch' }
    foreach ($name in $preserve) {
        if ((Get-FileHash -LiteralPath (Join-Path $game $name)).Hash -ne $before[$name]) {
            throw ('Preserved file changed: ' + $name)
        }
    }
}
catch {
    Copy-Item -LiteralPath (Join-Path $backup 'dxgi.dll') -Destination $target -Force
    throw
}
$record = [ordered]@{
    time=(Get-Date).ToString('o'); source_commit='a73885d1cb52b18dc0abad5a98312177ae572928'
    target=$target; backup=$backup; old_core_sha256=$oldExpected; deployed_core_sha256=$expected
    preserved_sha256=$before; game_tested=$false
}
[IO.File]::WriteAllText((Join-Path $backup 'deployment.json'), ($record | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
$record | ConvertTo-Json -Depth 4
