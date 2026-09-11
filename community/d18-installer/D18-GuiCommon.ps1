#Requires -Version 5.1
function Start-D18GuiOperation {
    param([hashtable]$Request, [string]$SessionRoot)
    $jobDir = Join-Path $SessionRoot ([guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $jobDir -Force
    $requestPath = Join-Path $jobDir 'request.json'
    $resultPath = Join-Path $jobDir 'result.json'
    $Request | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $requestPath -Encoding UTF8
    $spec = @{ worker=(Join-Path $PSScriptRoot 'D18-GuiWorker.ps1'); request=$requestPath; result=$resultPath }
    $data = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($spec | ConvertTo-Json -Compress)))
    $command = '$s=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("' + $data + '")) | ConvertFrom-Json; & $s.worker -RequestPath $s.request -ResultPath $s.result; exit $LASTEXITCODE'
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $process = Start-Process -FilePath $ps -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $jobDir 'worker.out.log') -RedirectStandardError (Join-Path $jobDir 'worker.err.log')
    return [pscustomobject]@{ process=$process; result=$resultPath; directory=$jobDir }
}
function Get-D18GuiRequest {
    param([string]$Action,[string]$Exe,[int]$ApiIndex,[string]$Proxy,[string]$Runtime,[string]$Ref='Auto',[bool]$Ack=$false)
    if (-not (Test-Path -LiteralPath $Exe -PathType Leaf) -or [IO.Path]::GetExtension($Exe) -ine '.exe') { throw '[GAME_EXE]' }
    if ($Action -ne 'Uninstall') {
        if ($ApiIndex -lt 0 -or $ApiIndex -gt 2) { throw '[API]' }
        if ($Proxy -notin @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll')) { throw '[PROXY]' }
        if (-not (Test-Path -LiteralPath $Runtime -PathType Leaf)) { throw '[RUNTIME_PATH]' }
    }
    return @{ action=$Action; exe=[IO.Path]::GetFullPath($Exe); api=@('DX11','None','Vulkan')[[Math]::Max(0,$ApiIndex)]; proxy=$Proxy; runtime=$Runtime; ref=$Ref; ack=$Ack }
}
