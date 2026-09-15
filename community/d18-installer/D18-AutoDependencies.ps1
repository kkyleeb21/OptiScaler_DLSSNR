#Requires -Version 5.1
# Preparation writes only to the cache. Game writes belong to the managed installer.
function Install-D18VcRuntime {
    param([string]$Cache)
    $null=New-Item -ItemType Directory -Path $Cache -Force
    $path=Join-Path $Cache 'vc_redist.x64.exe'
    Get-D18RemoteFile 'https://aka.ms/vc14/vc_redist.x64.exe' $path
    $sig=Get-AuthenticodeSignature -LiteralPath $path
    if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'Microsoft'){throw 'Microsoft runtime signature validation failed.'}
    $proc=Start-Process -FilePath $path -ArgumentList '/install','/passive','/norestart' -Verb RunAs -Wait -PassThru
    try {
        if($proc.ExitCode -notin @(0,1638,3010)){throw "VC++ installer exit code: $($proc.ExitCode)"}
        $system=Get-D18SystemDependencies 'None'
        if($system.vc_missing.Count){throw ('VC++ still missing after installation: '+($system.vc_missing -join ', '))}
        return @{reboot=($proc.ExitCode -eq 3010);verified=$true}
    } finally {$proc.Dispose()}
}
function Get-D18AutomaticDll {
    param([string]$Kind,[string]$Cache)
    # Pin the candidate's chosen production family, never silently follow latest.
    $catalog=Get-D18DownloadCatalog $Cache
    $entries=@($catalog.$Kind | Where-Object {$_.version -eq '310.9.1.0' -and $_.is_signature_valid -and -not $_.is_dev_file})
    if($entries.Count -ne 1){throw 'No unique supported production version. Select a version or local files manually.'}
    $name=if($Kind -eq 'dlss'){'nvngx_dlss.dll'}else{'nvngx_dlssg.dll'}
    return Get-D18SwapperDll $entries[0] $name $Cache
}
function Invoke-D18DependencyPreparation {
    param($Request,[string]$Cache,[string]$ResultDirectory,[switch]$Prepare)
    $game=[IO.Path]::GetFullPath([string]$Request.game).TrimEnd('\')
    if(-not(Test-Path -LiteralPath $game -PathType Container)){throw 'Select a game folder first.'}
    if($Request.api -notin @('None','DX11','Vulkan')){throw 'Select a graphics API first.'}
    # Do not allow a user-selected cache to turn preparation into a game deployment.
    foreach($path in @($Cache,$ResultDirectory)){
        $full=[IO.Path]::GetFullPath($path).TrimEnd('\')
        if($full -ieq $game -or $full.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Preparation cache must be outside the game folder.'}
        $cursor=$full
        while($cursor){
            if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Use a preparation cache without directory links.'}
            $cursor=Split-Path -Parent $cursor
        }
    }
    $rows=New-Object System.Collections.Generic.List[object]
    $files=New-Object System.Collections.Generic.List[object]
    $answer=[ordered]@{schema='d18-dependency-preparation-v1';game=$game;api=$Request.api;rows=$rows;plan='';runtime='';nr_source='';ref_path='';reboot=$false;ready=$false}
    function Row($id,$status,$zh,$en){$rows.Add([pscustomobject]@{id=$id;status=$status;zh=$zh;en=$en})}
    function File($source,$target){$files.Add(@{source=$source;target=$target;sha256=(Get-D18Sha256 $source);expected_absent=$true})}
    $work=$null
    if($Prepare){$work=Join-Path $Cache ('prepared-'+[guid]::NewGuid().ToString('N'));$null=New-Item -ItemType Directory -Path $work -Force}
    # NR is found in bounded local locations, never substituted with SR/RR or downloaded from an unverified source.
    $nrCandidates=if($Request.runtime){@([string]$Request.runtime)}else{@((Join-Path $game 'D24Runtime.dll'),(Join-Path $game 'nvngx_dlssnr.dll'),(Join-Path $PSScriptRoot 'runtime_input\nvngx_dlssnr.dll'),(Join-Path $Cache 'runtime_input\nvngx_dlssnr.dll'))}
    $nr=@($nrCandidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1)
    if(-not $nr.Count){Row 'NR' 'manual' '未找到 NR：请提供兼容的 nvngx_dlssnr.dll。SR/RR 文件不能代替。' 'NR missing: supply a compatible nvngx_dlssnr.dll. SR/RR cannot replace it.'}
    elseif(-not $Prepare){Row 'NR' 'found' ('找到本地 NR，补齐时校验：'+$nr[0]) ('Local NR found; validation pending: '+$nr[0])}
    else {
        try {
            $patched=Join-Path $work 'nr-validation.dll'
            $null=New-D18PatchedRuntime -SourcePath $nr[0] -OutputPath $patched
            if($Request.api -eq 'DX11'){$null=Test-D18Dx11Runtime -RuntimePath $patched -CheckerPath (Join-Path $PSScriptRoot 'payload\D18RuntimeCheck.exe')}
            # Content-addressed cache avoids another large NR copy on every retry.
            $hash=Get-D18Sha256 $patched
            $savedDir=Join-Path $Cache ('nr\'+$hash);$null=New-Item -ItemType Directory -Path $savedDir -Force
            $saved=Join-Path $savedDir 'nvngx_dlssnr.dll'
            if((Test-Path -LiteralPath $saved) -and (Get-D18Sha256 $saved) -ieq $hash){Remove-Item -LiteralPath $patched}
            else{Move-Item -LiteralPath $patched -Destination $saved -Force}
            $answer.runtime=$saved
            $answer.nr_source=$nr[0]
            Row 'NR' 'ready' '本地 NR 已通过补丁/布局检查；实机兼容性仍由游戏验证。' 'Local NR passed patch/layout checks; game compatibility remains untested.'
        } catch {Row 'NR' 'error' ('NR 需处理：'+$_.Exception.Message) ('NR needs attention: '+$_.Exception.Message)}
    }
    foreach($kind in @('SR','FG')){
        $enabled=if($kind -eq 'SR'){[bool]$Request.includeSr}else{[bool]$Request.includeFg}
        $mode=if($kind -eq 'SR'){$Request.srMode}else{$Request.fgMode}
        if(-not $enabled){Row $kind 'skipped' '未选择自动准备；保留游戏现有路线。' 'Automatic preparation not selected; keep the game route.';continue}
        if($mode -in @('Download','Local')){Row $kind 'selected' '保留你手动选择的来源，最终检查时准备。' 'Keep your manual source; prepare it during the final file check.';continue}
        $names=if($kind -eq 'SR'){@('nvngx_dlss.dll')}else{@('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')}
        $folder=if($kind -eq 'SR'){$game}else{Join-Path $game 'streamline'}
        $present=@($names | Where-Object {Test-Path -LiteralPath (Join-Path $folder $_)})
        try {
            foreach($name in $present){Assert-D18DependencyDll (Join-Path $folder $name) $name}
            if($present.Count -eq $names.Count){Row $kind 'kept' '已有完整 x64 文件，原样保留；不代表功能已在游戏生效。' 'Complete x64 files found and kept; this does not prove in-game operation.';continue}
            if(-not $Prepare){Row $kind 'missing' ('可补齐：'+(@($names | Where-Object {$_ -notin $present}) -join ', ')) ('Can prepare: '+(@($names | Where-Object {$_ -notin $present}) -join ', '));continue}
            $sources=@{}
            if($kind -eq 'SR'){$sources['nvngx_dlss.dll']=Get-D18AutomaticDll 'dlss' $Cache}
            else {
                $bundle=Get-D18StreamlineBundle $Cache
                foreach($name in $names){$sources[$name]=if($name -eq 'nvngx_dlssg.dll'){Get-D18AutomaticDll 'dlss_g' $Cache}else{Join-Path $bundle $name}}
            }
            # An incomplete unknown bundle must not be silently mixed with a new version.
            foreach($name in $present){if((Get-D18Sha256 (Join-Path $folder $name)) -ine (Get-D18Sha256 $sources[$name])){throw 'Existing partial bundle differs. Keep it, or explicitly select a complete local/download bundle.'}}
            foreach($name in $names){
                Assert-D18DependencyDll $sources[$name] $name
                if($name -notin $present){File $sources[$name] $(if($kind -eq 'SR'){$name}else{'streamline\'+$name})}
            }
            Row $kind 'ready' '缺失文件已下载并校验，等待最后安装；已有文件不变。' 'Missing files downloaded and verified, awaiting final installation; existing files unchanged.'
        } catch {Row $kind 'error' ($kind+' 需处理：'+$_.Exception.Message) ($kind+' needs attention: '+$_.Exception.Message)}
    }
    try {
        $profile=Get-D18ReProfile -Game $game -ForceRE:([bool]$Request.reEngine)
        $existing=Join-Path $game 'dinput8.dll'
        if(-not $profile.IsRE){Row 'REF' 'skipped' '当前游戏无需 REFramework。' 'REFramework is not required for this game.'}
        elseif($Request.refPath -or $Request.ref -notin @('Auto','Existing','Recommended')){Row 'REF' 'selected' '保留手动选择的 REFramework 来源。' 'Keep the selected REFramework source.'}
        elseif(Test-Path -LiteralPath $existing){
            Assert-D18DependencyDll $existing 'dinput8.dll'
            if((Get-D18Sha256 $existing) -ine $profile.Catalog.tested.dll_sha256 -and -not $Request.refConfirm){throw 'Unrecognized dinput8.dll; confirm it is REFramework in the dependency options.'}
            Row 'REF' 'kept' '已有 REFramework，保留原文件和配置。' 'Existing REFramework retained with its configuration.'
        }
        elseif(-not $profile.Tested){Row 'REF' 'manual' '无匹配记录，请手动选择 REFramework 版本或本地文件。' 'No matched build; select a REFramework version or local file.'}
        elseif(-not $Prepare){Row 'REF' 'missing' '可下载已匹配的官方 REFramework。' 'The matched official REFramework build can be downloaded.'}
        else {$answer.ref_path=Get-D18RefDownload -Mode Recommended -Profile $profile -Stage $work;Row 'REF' 'ready' '匹配的 REFramework 已校验，等待最后安装。' 'Matched REFramework verified and awaiting final installation.'}
    } catch {Row 'REF' 'error' $_.Exception.Message $_.Exception.Message}
    $system=Get-D18SystemDependencies $Request.api
    if(-not $system.vc_missing.Count){Row 'VC++' 'kept' 'VC++ x64 依赖文件已找到。' 'VC++ x64 dependency files found.'}
    elseif(-not $Prepare -or -not $Request.installVc){Row 'VC++' 'missing' '缺少 VC++ x64；勾选自动安装后可补齐，Windows 可能弹出 UAC。' 'VC++ x64 is missing; select automatic installation. Windows may show UAC.'}
    else {
        try {$vc=Install-D18VcRuntime $Cache;$answer.reboot=$vc.reboot;Row 'VC++' 'ready' $(if($vc.reboot){'VC++ 已安装，需要重启 Windows。'}else{'VC++ 已安装并重新检查。'}) $(if($vc.reboot){'VC++ installed; Windows restart required.'}else{'VC++ installed and rechecked.'})}
        catch {Row 'VC++' 'error' $_.Exception.Message $_.Exception.Message}
    }
    if($system.graphics_missing.Count){Row '系统图形 / Graphics' 'manual' ('缺少系统图形组件，请通过 Windows/显卡驱动安装程序修复：'+($system.graphics_missing -join ', ')) ('Repair through Windows/GPU driver setup: '+($system.graphics_missing -join ', '))}
    if($Prepare){
        $answer.plan=Join-Path $work 'dependencies.json'
        @{schema='d18-auto-dependencies-v1';game=$game;api=$Request.api;files=@($files.ToArray())}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $answer.plan -Encoding UTF8
    }
    $answer.ready=(@($rows | Where-Object {$_.status -in @('missing','manual','error','found')}).Count -eq 0 -and -not $answer.reboot)
    return $answer
}
