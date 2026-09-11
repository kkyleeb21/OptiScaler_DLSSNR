param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$ProcessName='Endfield',
    [string]$RestoreFrom
)
$ErrorActionPreference='Stop'
if (Get-Process -Name $ProcessName -ErrorAction SilentlyContinue) { throw "Exit $ProcessName before changing startup logging" }
$game=(Resolve-Path -LiteralPath $GameDirectory).Path
$ini=Join-Path $game 'OptiScaler.ini'
$text=[IO.File]::ReadAllText($ini)
$pattern='(?ms)^\[Log\][^\r\n]*\r?\n.*?(?=^\[|\z)'
$section=[regex]::Match($text,$pattern)
if (!$section.Success) { throw 'Log section missing' }
$backup=Join-Path 'E:\DLSSNR\backups' ('StartupLog-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $ini -Destination (Join-Path $backup 'OptiScaler.ini')
if ((Get-FileHash -LiteralPath $ini).Hash -ne (Get-FileHash -LiteralPath (Join-Path $backup 'OptiScaler.ini')).Hash) { throw 'Backup hash mismatch' }
$logPath=Join-Path $game 'D18Startup.log'
if ($RestoreFrom) {
    $prior=[regex]::Match([IO.File]::ReadAllText($RestoreFrom),$pattern)
    if (!$prior.Success) { throw 'Restore source has no Log section' }
    $updated=$prior.Value
} else {
    $updated=$section.Value
    $settings=[ordered]@{LogToFile='true';LogLevel='1';LogAsync='false';LogFileName=$logPath}
    foreach ($key in $settings.Keys) {
        $rx='(?m)^'+[regex]::Escape($key)+'\s*=[^\r\n]*'
        if (![regex]::IsMatch($updated,$rx)) { throw "Missing logging key $key" }
        $replacement=$key+' = '+$settings[$key]
        $updated=[regex]::Replace($updated,$rx,[System.Text.RegularExpressions.MatchEvaluator]{param($m) $replacement})
    }
    if (Test-Path -LiteralPath $logPath) { Copy-Item -LiteralPath $logPath -Destination $backup }
}
$result=$text.Substring(0,$section.Index)+$updated+$text.Substring($section.Index+$section.Length)
[IO.File]::WriteAllText($ini,$result,[Text.UTF8Encoding]::new($false))
if ([IO.File]::ReadAllText($ini) -cne $result) { throw 'Config readback mismatch' }
@{Game=$game;Backup=$backup;Log=$logPath;Restored=[bool]$RestoreFrom;ConfigHash=(Get-FileHash -LiteralPath $ini).Hash} | ConvertTo-Json | Tee-Object -FilePath (Join-Path $backup 'verified.json')
