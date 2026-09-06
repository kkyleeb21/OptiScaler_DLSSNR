#Requires -Version 5.1
# Downloads are deliberately restricted to the user's chosen official nightly repository.
$script:D18RefRepository = 'praydog/REFramework-nightly'

function Get-D18ReProfile {
    param([string]$Game, [switch]$ForceRE)
    $catalog = Get-Content (Join-Path $PSScriptRoot 'reframework-versions.json') -Raw | ConvertFrom-Json
    $exes = @(Get-ChildItem -LiteralPath $Game -Filter '*.exe' -File | Select-Object -ExpandProperty Name)
    $known = @($exes | Where-Object { $catalog.re_games -icontains $_ })
    $tested = @($exes | Where-Object { $catalog.tested.games -icontains $_ })
    return [pscustomobject]@{ IsRE=($ForceRE -or $known.Count -gt 0); Tested=($tested.Count -gt 0); Catalog=$catalog }
}

function Assert-D18GameStopped {
    param([string]$Game)
    $names = @(Get-ChildItem -LiteralPath $Game -Filter '*.exe' -File | Select-Object -ExpandProperty BaseName)
    foreach ($name in $names) {
        if (Get-Process -Name $name -ErrorAction SilentlyContinue) { throw "Close the game before installing: $name" }
    }
}

function Select-D18RefAsset {
    param($Release)
    # Never choose VR.zip, C# packages or a similarly named third-party repository.
    $assets = @($Release.assets | Where-Object { $_.name -ceq 'REFramework.zip' })
    if ($assets.Count -ne 1) { throw 'No unique universal REFramework.zip in this nightly. Install REFramework manually.' }
    $asset = $assets[0]
    $url = [uri]$asset.browser_download_url
    if ($url.Scheme -cne 'https' -or $url.Host -ine 'github.com' -or
        -not $url.AbsolutePath.StartsWith('/praydog/REFramework-nightly/releases/download/', [StringComparison]::Ordinal) -or
        $url.Query -or $url.Fragment -or $url.UserInfo) { throw 'Rejected non-official REFramework download URL.' }
    if (-not ($asset.PSObject.Properties.Name -contains 'digest') -or $asset.digest -notmatch '^sha256:[a-fA-F0-9]{64}$') {
        throw 'Official download has no SHA256 digest. Install REFramework manually.'
    }
    return $asset
}

function Get-D18RefDownload {
    param([string]$Mode, $Profile, [string]$Stage)
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $headers = @{ 'User-Agent'='DLSSNR-D18-Installer'; 'Accept'='application/vnd.github+json' }
    $suffix = if ($Mode -eq 'Recommended') { 'tags/' + $Profile.Catalog.tested.tag } else { 'latest' }
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$script:D18RefRepository/releases/$suffix" -Headers $headers
    $asset = Select-D18RefAsset $release
    $expected = ([string]$asset.digest).Substring(7)
    if ($Mode -eq 'Recommended' -and $expected -ine $Profile.Catalog.tested.sha256) { throw 'Recommended nightly digest changed.' }
    $zip = Join-Path $Stage 'REFramework.zip'
    Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $zip -Headers $headers
    if ((Get-D18Sha256 $zip) -ine $expected) { throw 'REFramework download checksum mismatch.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $entries = @($archive.Entries | Where-Object { $_.FullName -ceq 'dinput8.dll' })
        if ($entries.Count -ne 1 -or $entries[0].Length -gt 128MB) { throw 'Unexpected REFramework archive layout.' }
        $dll = Join-Path $Stage 'dinput8.dll'
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], $dll, $false)
    } finally { $archive.Dispose() }
    $bytes = [IO.File]::ReadAllBytes($dll)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 77 -or $bytes[1] -ne 90) { throw 'Invalid REFramework PE image.' }
    $offset = [BitConverter]::ToInt32($bytes,60)
    if ($offset -lt 0 -or $offset -gt $bytes.Length-24 -or [BitConverter]::ToUInt32($bytes,$offset) -ne 17744 -or
        [BitConverter]::ToUInt16($bytes,$offset+4) -ne 34404) { throw 'REFramework must be an x64 DLL.' }
    if ($Mode -eq 'Recommended' -and (Get-D18Sha256 $dll) -ine $Profile.Catalog.tested.dll_sha256) { throw 'Recommended DLL changed.' }
    Write-Host "REFramework: $($release.tag_name), official SHA256 verified."
    return $dll
}

function Set-D18RefMenuKey {
    param([string]$Text)
    $pattern = '(?m)^[ \t]*REFrameworkConfig_MenuKey_V2[ \t]*=[^\r\n]*'
    if ([regex]::Matches($Text,$pattern).Count -gt 1) { throw 'Duplicate REFramework menu-key settings.' }
    if ([regex]::IsMatch($Text,$pattern)) { return [regex]::Replace($Text,$pattern,'REFrameworkConfig_MenuKey_V2=34') }
    return $Text.TrimEnd()+"`r`nREFrameworkConfig_MenuKey_V2=34`r`n"
}

function Add-D18RefItems {
    param([string]$Game, $Profile, [string]$Mode, [string]$Stage, $Items, [switch]$AssumeYes)
    if (-not $Profile.IsRE) { return }
    Write-Host 'RE Engine: REFramework is required for the supported installation. Menu key: PgDn.'
    $existing = Join-Path $Game 'dinput8.dll'
    if ($Mode -eq 'Auto') {
        if (Test-Path -LiteralPath $existing) { $Mode = 'Existing' }
        elseif ($Profile.Tested) {
            if ($AssumeYes) { $Mode='Recommended' }
            else {
                $answer = Read-Host 'Install the matched official REFramework nightly? [Y/n]'
                $Mode = if ($answer -match '^(n|no)$') { 'Manual' } else { 'Recommended' }
            }
        } else {
            if ($AssumeYes) { throw 'No matched REFramework version. Choose -REFramework Latest or Manual explicitly.' }
            Write-Host 'No matched version: 1. Latest official nightly (compatibility unverified)  2. Install manually'
            $Mode = if ((Read-Host 'Choice [2]') -eq '1') { 'Latest' } else { 'Manual' }
        }
    }
    $dll = $null
    if (Test-Path -LiteralPath $existing) {
        $recognized = (Get-D18Sha256 $existing) -ieq $Profile.Catalog.tested.dll_sha256
        if (-not $recognized) {
            # A filename or config file alone is not proof of DLL identity. Never overwrite another loader.
            if ($AssumeYes) { throw 'Unrecognized dinput8.dll. Rerun interactively to confirm existing REFramework, or resolve the loader conflict.' }
            if ((Read-Host 'Existing dinput8.dll is not the matched build. Confirm it is REFramework: type REFRAMEWORK') -cne 'REFRAMEWORK') {
                throw 'Existing dinput8.dll retained; resolve the loader conflict before continuing.'
            }
        }
        if ($Mode -in @('Recommended','Latest')) { throw 'Existing REFramework retained. Update it separately or choose Existing.' }
        # Stage the current DLL before managed uninstall/reinstall removes the previous managed copy.
        $dll = Join-Path $Stage 'existing-dinput8.dll'
        Copy-Item -LiteralPath $existing -Destination $dll
    } elseif ($Mode -eq 'Existing') { throw 'No existing REFramework dinput8.dll found.' }
    elseif ($Mode -in @('Recommended','Latest')) {
        if ($Mode -eq 'Recommended' -and -not $Profile.Tested) { throw 'No matched build for this game. Choose Latest or Manual.' }
        try { $dll = Get-D18RefDownload -Mode $Mode -Profile $Profile -Stage $Stage }
        catch {
            Write-Host "REFramework could not be prepared: $($_.Exception.Message)" -ForegroundColor Yellow
            if ($AssumeYes) { throw }
            Write-Host '1. Try latest official nightly  2. Install REFramework manually  3. Cancel'
            $choice=Read-Host 'Choice [3]'
            if ($choice -eq '1' -and $Mode -ne 'Latest') { $dll=Get-D18RefDownload -Mode Latest -Profile $Profile -Stage $Stage }
            elseif ($choice -eq '2') { $Mode='Manual' }
            else { throw 'Installation cancelled; game files were not changed.' }
        }
    }
    if ($dll) {
        $Items.Add([pscustomobject]@{Source=$dll;TargetRelative='dinput8.dll';ExpectedHash=(Get-D18Sha256 $dll)})
    } else {
        Write-Host 'Install REFramework before launching: https://github.com/praydog/REFramework-nightly/releases' -ForegroundColor Yellow
    }
    # Only edit the menu key, leaving scripts, plugins and all other REF options intact.
    $config = Join-Path $Game 're2_fw_config.txt'
    $text = if (Test-Path -LiteralPath $config) { [IO.File]::ReadAllText($config) } else { '' }
    $stagedConfig = Join-Path $Stage 're2_fw_config.txt'
    [IO.File]::WriteAllText($stagedConfig,(Set-D18RefMenuKey $text),[Text.UTF8Encoding]::new($false))
    $Items.Add([pscustomobject]@{Source=$stagedConfig;TargetRelative='re2_fw_config.txt';ExpectedHash=(Get-D18Sha256 $stagedConfig)})
    # Mirror owned binaries only. D18 0.1.3 always uses the root INI even when loaded from _storage_.
    foreach ($item in @($Items.ToArray())) {
        if ($item.TargetRelative -match '(?i)\.dll$') {
            $Items.Add([pscustomobject]@{Source=$item.Source;TargetRelative=('_storage_\'+$item.TargetRelative);ExpectedHash=$item.ExpectedHash})
        }
    }
}
