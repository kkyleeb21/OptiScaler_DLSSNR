[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$OutputDirectory = (Join-Path 'E:\DLSSNR\evidence\D18' (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [string]$BuildManifest,
    [string]$LayerCaptureDirectory
)
$ErrorActionPreference = 'Stop'
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$ring = Join-Path $game 'D18Diagnostics.ring'
$diagnosticNames = @('D18Diagnostics.ring','D18WildlandsSR.jsonl','D18UiDiagnostics.jsonl','D18ExecutionTrace.jsonl','D18InputProbe.jsonl','D18Dx11Debug.jsonl','D18PostReplay.jsonl','D18CommandLists.jsonl','D18CommandConflicts.jsonl')
$available = @($diagnosticNames | Where-Object { Test-Path -LiteralPath (Join-Path $game $_) -PathType Leaf })
if (!$available.Count -and !$LayerCaptureDirectory) { throw 'No supported D18 diagnostic logs found' }
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $out.StartsWith('E:\DLSSNR\evidence\D18\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must stay under E:\DLSSNR\evidence\D18'
}
New-Item -ItemType Directory -Path $out -Force | Out-Null
foreach ($name in $available) { Copy-Item -LiteralPath (Join-Path $game $name) -Destination $out }
foreach ($name in @('OptiScaler.log', 'OptiScaler.ini','D18SrDiagnostics.panel.enabled','D18SrDiagnostics.evaluate-only','D18SrDiagnostics.upscale-output.enabled','D18SrDiagnostics.post-replay.enabled','D18SrDiagnostics.native-handoff.enabled','D18WildlandsSR.start-disabled','D18UiDiagnostics.enabled','D18Dx11Debug.enabled')) {
    $candidate = Join-Path $game $name
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { Copy-Item -LiteralPath $candidate -Destination $out }
}
if ($BuildManifest -and (Test-Path -LiteralPath $BuildManifest -PathType Leaf)) {
    Copy-Item -LiteralPath $BuildManifest -Destination (Join-Path $out 'build-manifest.json')
}
$python = (Get-Command python -ErrorAction Stop).Source
if ($available -contains 'D18Diagnostics.ring') {
& $python (Join-Path $PSScriptRoot 'summarize-render-diagnostics.py') (Join-Path $out 'D18Diagnostics.ring') `
    --json (Join-Path $out 'summary.json') --markdown (Join-Path $out 'summary.md')
if ($LASTEXITCODE -ne 0) { throw "summarizer failed with exit code $LASTEXITCODE" }
}
if ($available -contains 'D18WildlandsSR.jsonl') {
    $srArgs = @((Join-Path $PSScriptRoot 'summarize-input-sr.py'),(Join-Path $out 'D18WildlandsSR.jsonl'),'--output',(Join-Path $out 'sr-summary.json'))
    if ($available -contains 'D18UiDiagnostics.jsonl') { $srArgs += @('--ui-log',(Join-Path $out 'D18UiDiagnostics.jsonl')) }
    if ($available -contains 'D18ExecutionTrace.jsonl') { $srArgs += @('--execution-log',(Join-Path $out 'D18ExecutionTrace.jsonl')) }
    & $python @srArgs | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "SR summarizer failed with exit code $LASTEXITCODE; raw logs retained" }
}
if ($available -contains 'D18InputProbe.jsonl') {
    & $python (Join-Path $PSScriptRoot 'summarize-input-probe.py') (Join-Path $out 'D18InputProbe.jsonl') --output (Join-Path $out 'input-summary.json') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Input summarizer failed with exit code $LASTEXITCODE; raw logs retained" }
}
if ($available -contains 'D18Dx11Debug.jsonl') {
 & $python (Join-Path $PSScriptRoot 'summarize-dx11-debug.py') (Join-Path $out 'D18Dx11Debug.jsonl') --output (Join-Path $out 'dx11-debug-summary.json')
 if ($LASTEXITCODE -ne 0) { throw 'DX11 debug summarizer failed; raw retained' }
}
if ($available -contains 'D18PostReplay.jsonl') {
    & $python (Join-Path $PSScriptRoot 'summarize-writer-journal.py') (Join-Path $out 'D18PostReplay.jsonl') --output (Join-Path $out 'writer-summary.json')
    if ($LASTEXITCODE -ne 0) { throw 'Writer journal summary failed; raw evidence retained' }
}
if ($available -contains 'D18CommandConflicts.jsonl') {
    & $python (Join-Path $PSScriptRoot 'summarize-command-lists.py') (Join-Path $out 'D18CommandConflicts.jsonl') --output (Join-Path $out 'command-conflict-summary.json')
    if ($LASTEXITCODE -ne 0) { throw 'Command-conflict summary failed; raw evidence retained' }
}
if ($available -contains 'D18CommandLists.jsonl') {
    & $python (Join-Path $PSScriptRoot 'summarize-command-lists.py') (Join-Path $out 'D18CommandLists.jsonl') --output (Join-Path $out 'command-list-summary.json')
    if ($LASTEXITCODE -ne 0) { throw 'Command-list summary failed; raw evidence retained' }
}
if ($LayerCaptureDirectory) {
    & $python (Join-Path $PSScriptRoot 'archive-layer-capture.py') $LayerCaptureDirectory (Join-Path $out 'layer-capture')
    if ($LASTEXITCODE -ne 0) { throw 'Layer capture archive/validation failed; copied evidence retained' }
}
if ((Test-Path -LiteralPath (Join-Path $game 'D18PostCompute.jsonl')) -and (Test-Path -LiteralPath (Join-Path $game 'D18PostCompute.bin')) -and ($available -contains 'D18PostReplay.jsonl')) {
    $computeIndex=Join-Path $game 'D18PostCompute.jsonl'
    $computeBin=Join-Path $game 'D18PostCompute.bin'
    if ((Get-Item -LiteralPath $computeIndex).Length -gt 262144 -or (Get-Item -LiteralPath $computeBin).Length -gt 16777216) { throw 'Compute archive exceeds bounded capture budget' }
    Copy-Item -LiteralPath $computeIndex,$computeBin -Destination $out
    & $python (Join-Path $PSScriptRoot 'analyze-compute-journal.py') $out
    if ($LASTEXITCODE -ne 0) { throw 'Compute archive analysis failed; evidence retained' }
}
[ordered]@{schema='d18-diagnostic-bundle-v1';source=$game;collected_at=(Get-Date -Format o);logs=$available;boundary='Independent file snapshots; not an atomic live capture or gameplay acceptance';layer_capture_requested=[bool]$LayerCaptureDirectory;binary_archives='Layer capture only when explicitly selected; other binary archives not copied automatically'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'collection.json') -Encoding utf8
Get-ChildItem -LiteralPath $out -File | Get-FileHash -Algorithm SHA256 |
    Select-Object @{n='File';e={Split-Path $_.Path -Leaf}},Hash |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'sha256.json') -Encoding utf8
Write-Output $out
