function Assert-OnimushaPrerequisites {
    param([string]$Game)
    foreach ($relative in @('', '_storage_', 'D18_Backups', 'OptiScaler', '_storage_\OptiScaler')) {
        $candidate = if ($relative) { Join-Path $Game $relative } else { $Game }
        if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Linked game/deployment directory is not supported by this preview: $candidate"
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Game 'OnimushaWotS.exe') -PathType Leaf)) {
        throw 'Onimusha ONLY: select the folder containing OnimushaWotS.exe.'
    }
    if (Get-Process -Name OnimushaWotS,CrashReport -ErrorAction SilentlyContinue) {
        throw 'Close Onimusha and its crash reporter before installation.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Game 'dinput8.dll') -PathType Leaf)) {
        throw 'Install the Onimusha-specific REFramework from Nexus Mods first (dinput8.dll beside OnimushaWotS.exe).'
    }
    Write-Host 'REFramework prerequisite file found. This alone does not verify the installed REF build.'
    foreach ($name in @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll')) {
        if (Test-Path -LiteralPath (Join-Path $Game $name)) {
            throw "Conflicting proxy candidate: $name. Back up and resolve your existing mod-loader chain first; nothing was removed."
        }
    }
}

function Merge-OnimushaIni {
    param([string]$Existing, [string]$Defaults)
    $text = if (Test-Path -LiteralPath $Existing -PathType Leaf) { [IO.File]::ReadAllText($Existing) }
            else { [IO.File]::ReadAllText($Defaults) }
    # Only compatibility-critical keys are enforced. User NR/style/ratio settings survive upgrades.
    $settings = @{
        Upscalers = @{ Dx12Upscaler='dlss' }
        Hotfix = @{ SkipStreamlineHooks='true'; RestoreComputeSignature='true'; ExtendedStateRestore='true' }
        DlssNr = @{ NgxOnlyMode='true'; WorkingScale='1.0' }
        Menu = @{ OverlayMenu='true' }
    }
    foreach ($section in $settings.Keys) {
        $pattern='(?ms)^\[' + [regex]::Escape($section) + '\][^\r\n]*\r?\n.*?(?=^\[|\z)'
        $match=[regex]::Match($text,$pattern)
        $block=if($match.Success){$match.Value}else{"[$section]`r`n"}
        foreach($key in $settings[$section].Keys) {
            $keyPattern='(?m)^\s*' + [regex]::Escape($key) + '\s*=[^\r\n]*'
            $line=$key+'='+$settings[$section][$key]
            if([regex]::IsMatch($block,$keyPattern)){$block=[regex]::Replace($block,$keyPattern,$line)}
            else {$block=$block.TrimEnd()+"`r`n"+$line+"`r`n"}
        }
        if($match.Success){$text=$text.Remove($match.Index,$match.Length).Insert($match.Index,$block+"`r`n")}
        else{$text=$text.TrimEnd()+"`r`n`r`n"+$block}
    }
    return $text
}
