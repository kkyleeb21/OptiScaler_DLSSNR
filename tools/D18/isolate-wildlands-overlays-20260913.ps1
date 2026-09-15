$ErrorActionPreference = 'Stop'
$game = 'D:\SteamLibrary\steamapps\common\Wildlands'
$ini = Join-Path $game 'OptiScaler.ini'
$core = Join-Path $game 'dxgi.dll'
if (Get-Process GRW -ErrorAction SilentlyContinue) { throw 'GRW is running; no changes made.' }
$expected = '44725543D1804227ECFE5BDE054F88BA158B0D1DAA1FEF7EA06DDA9E2CDC17DD'
$expectedCore = 'AB37416A057A399B652BE32D36FC40B590933465F32E1287F92547B91667B82E'
if ((Get-FileHash -LiteralPath $ini).Hash -ne $expected) { throw 'Configuration changed since inspection.' }
if ((Get-FileHash -LiteralPath $core).Hash -ne $expectedCore) { throw 'Core changed since inspection.' }
if (!(Test-Path -LiteralPath (Join-Path $game 'D18WildlandsSR.start-disabled'))) { throw 'SR start-disabled marker missing.' }
$encoding = [Text.Encoding]::GetEncoding(28591)
$before = [IO.File]::ReadAllBytes($ini)
$text = $encoding.GetString($before)
$pattern = '(?m)^DisableOverlays=auto(?=\r?$)'
if ([regex]::Matches($text, $pattern).Count -ne 1) { throw 'Expected exactly one overlay setting.' }
$after = $encoding.GetBytes([regex]::Replace($text, $pattern, 'DisableOverlays=true'))
$backup = Join-Path 'E:\DLSSNR\backups' ('Wildlands-BeforeOverlayIsolation-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $ini -Destination (Join-Path $backup 'OptiScaler.ini')
if ((Get-FileHash -LiteralPath (Join-Path $backup 'OptiScaler.ini')).Hash -ne $expected) { throw 'Backup hash mismatch.' }
foreach ($name in @('D18WildlandsSR.jsonl','D18UiDiagnostics.jsonl','D18ExecutionTrace.jsonl')) {
    $path = Join-Path $game $name
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $backup }
}
if (Get-Process GRW -ErrorAction SilentlyContinue) { throw 'GRW started; aborting before write.' }
if ((Get-FileHash -LiteralPath $ini).Hash -ne $expected) { throw 'Config changed before write.' }
[IO.File]::WriteAllBytes($ini, $after)
$actual = [IO.File]::ReadAllBytes($ini)
if ([Convert]::ToBase64String($actual) -ne [Convert]::ToBase64String($after)) { throw 'Written bytes mismatch.' }
if ((Get-FileHash -LiteralPath $core).Hash -ne $expectedCore) { throw 'Core changed during deployment.' }
$record = [ordered]@{
    time=(Get-Date -Format o); game=$game; backup=$backup
    config_before=$expected; config_after=(Get-FileHash -LiteralPath $ini).Hash
    core_sha256=$expectedCore; change='Hotfix.DisableOverlays: auto -> true'
    sr_starts_disabled=(Test-Path -LiteralPath (Join-Path $game 'D18WildlandsSR.start-disabled'))
    status='Temporary isolation configured; actual overlay hook exclusion requires live verification'
}
$json = $record | ConvertTo-Json
$json | Set-Content -LiteralPath (Join-Path $backup 'isolation.json') -Encoding UTF8
$json | Set-Content -LiteralPath 'E:\DLSSNR\reports\D18_WILDLANDS_OVERLAY_ISOLATION_20260913.json' -Encoding UTF8
$json
