[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$OutputDirectory = (Join-Path 'E:\DLSSNR\evidence\D18' (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [string]$BuildManifest
)
$ErrorActionPreference = 'Stop'
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$ring = Join-Path $game 'D18Diagnostics.ring'
if (-not (Test-Path -LiteralPath $ring -PathType Leaf)) { throw "D18Diagnostics.ring not found in the game directory" }
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $out.StartsWith('E:\DLSSNR\evidence\D18', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must stay under E:\DLSSNR\evidence\D18'
}
New-Item -ItemType Directory -Path $out -Force | Out-Null
Copy-Item -LiteralPath $ring -Destination (Join-Path $out 'D18Diagnostics.ring')
foreach ($name in @('OptiScaler.log', 'OptiScaler.ini')) {
    $candidate = Join-Path $game $name
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { Copy-Item -LiteralPath $candidate -Destination $out }
}
if ($BuildManifest -and (Test-Path -LiteralPath $BuildManifest -PathType Leaf)) {
    Copy-Item -LiteralPath $BuildManifest -Destination (Join-Path $out 'build-manifest.json')
}
$python = (Get-Command python -ErrorAction Stop).Source
& $python 'E:\DLSSNR\tools\D18\summarize-diagnostics.py' (Join-Path $out 'D18Diagnostics.ring') `
    --json (Join-Path $out 'summary.json') --markdown (Join-Path $out 'summary.md')
if ($LASTEXITCODE -ne 0) { throw "summarizer failed with exit code $LASTEXITCODE" }
Get-ChildItem -LiteralPath $out -File | Get-FileHash -Algorithm SHA256 |
    Select-Object @{n='File';e={Split-Path $_.Path -Leaf}},Hash |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'sha256.json') -Encoding utf8
Write-Output $out
