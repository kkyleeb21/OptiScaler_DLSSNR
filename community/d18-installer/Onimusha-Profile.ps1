function Set-D18RefFields {
    param([string]$Text, $Fields)
    foreach ($key in $Fields.Keys) {
        $pattern='(?m)^[ \t]*'+[regex]::Escape($key)+'[ \t]*=[^\r\n]*'
        $matchesFound=[regex]::Matches($Text,$pattern)
        if($matchesFound.Count -gt 1){throw "Duplicate REF setting: $key. Resolve it before installation."}
        $line=$key+'='+$Fields[$key]
        if($matchesFound.Count){$Text=[regex]::Replace($Text,$pattern,$line)}
        else{$Text=$Text.TrimEnd()+"`r`n"+$line+"`r`n"}
    }
    return $Text
}

function Restore-D18RefFields {
    param([string]$Current, [string]$Original, $Fields)
    foreach($property in $Fields.PSObject.Properties) {
        $key=$property.Name
        if($key -notin @('REFrameworkConfig_MenuKey_V2','REFrameworkConfig_MenuOpen','REFrameworkConfig_RememberMenuState')) {
            throw "Unknown managed REF setting: $key"
        }
        $pattern='(?m)^[ \t]*'+[regex]::Escape($key)+'[ \t]*=([^\r\n]*)'
        $now=[regex]::Matches($Current,$pattern)
        $old=[regex]::Matches($Original,$pattern)
        if($now.Count -gt 1 -or $old.Count -gt 1){throw "Duplicate REF setting: $key"}
        # User deletion or a changed value takes precedence over our rollback.
        if($now.Count -eq 0 -or $now[0].Groups[1].Value.Trim() -ne [string]$property.Value){continue}
        $replacement=if($old.Count){$old[0].Value}else{''}
        $Current=$Current.Remove($now[0].Index,$now[0].Length).Insert($now[0].Index,$replacement)
    }
    return $Current
}

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
