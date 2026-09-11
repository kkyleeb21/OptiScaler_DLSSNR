$ErrorActionPreference = 'Stop'
$baseline = 'E:\DLSSNR\releases\D18_0.1.1_Installer_Prompts_RC2'
$stage = 'E:\DLSSNR\builds\D18-0.1.2-package-input'
$manifest = Get-Content -LiteralPath (Join-Path $baseline 'payload_manifest.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    $file = Join-Path (Join-Path $baseline 'payload') $entry.path
    if ((Get-FileHash -LiteralPath $file).Hash -ne $entry.sha256) { throw "Baseline mismatch: $file" }
}
if (Test-Path -LiteralPath $stage) { throw 'Stage already exists' }
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($name in @('Licenses','OptiScaler','nvngx.dll_dlssnr.dll')) {
    Copy-Item -LiteralPath (Join-Path "$baseline\payload" $name) -Destination $stage -Recurse
}
Copy-Item -LiteralPath 'E:\DLSSNR\builds\D18CommonUiDiagnostics-20260906\OptiScaler.dll' -Destination "$stage\dxgi.dll"
$ini = [IO.File]::ReadAllText("$baseline\payload\OptiScaler.ini.d18")
$defaults = @('Diagnostics=0','ExperimentalCompose=false','GuidedReconstruction=false','GainFirstReconstruction=false','CatmullRomInput=false','FrequencyRadius=2.0','LumaTrust=1.0','ChromaTrust=1.0')
foreach ($line in $defaults) {
    $key = $line.Split('=')[0]
    if ($ini -match "(?m)^$key=") { throw "Unexpected existing new key: $key" }
}
if ([regex]::Matches($ini, '(?m)^\[DlssNr\]\r?$').Count -ne 1) { throw 'DlssNr section mismatch' }
$ini = [regex]::Replace($ini, '(?m)^\[DlssNr\]\r?$', ('[DlssNr]' + "`r`n" + ($defaults -join "`r`n")))
[IO.File]::WriteAllText("$stage\OptiScaler.ini", $ini, [Text.UTF8Encoding]::new($false))
Write-Output $stage
