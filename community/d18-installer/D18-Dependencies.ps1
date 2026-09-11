#Requires -Version 5.1
# Optional files use the normal installer transaction. No DLL is loaded for identification.
function Get-D18SystemDependencies {
    param([string]$Api)
    $system = [Environment]::GetFolderPath('System')
    $vc = @('MSVCP140.dll','MSVCP140_ATOMIC_WAIT.dll','VCRUNTIME140.dll','VCRUNTIME140_1.dll')
    $missing = @($vc | Where-Object { -not (Test-Path -LiteralPath (Join-Path $system $_)) })
    $graphics = @('D3DCOMPILER_47.dll','d3d12.dll','dxgi.dll')
    if ($Api -eq 'Vulkan') { $graphics += 'vulkan-1.dll' }
    return [pscustomobject]@{ vc_missing=$missing; graphics_missing=@($graphics | Where-Object { -not(Test-Path -LiteralPath (Join-Path $system $_)) }); driver_tested=$false }
}
function Get-D18RemoteFile {
    param([string]$Url,[string]$Destination)
    if (([uri]$Url).Scheme -ne 'https') { throw 'Downloads require HTTPS.' }
    [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile ($Destination+'.part') -Headers @{'User-Agent'='D18-Setup'}
    Move-Item -LiteralPath ($Destination+'.part') -Destination $Destination -Force
}
function Get-D18DownloadCatalog {
    param([string]$Cache)
    $null=New-Item -ItemType Directory -Path $Cache -Force
    $path=Join-Path $Cache 'swapper-manifest.json'
    try { Get-D18RemoteFile 'https://raw.githubusercontent.com/beeradmoore/dlss-swapper-manifest-builder/main/manifest.json' $path }
    catch { if(-not(Test-Path -LiteralPath $path)){throw} }
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}
function Assert-D18DependencyDll {
    param([string]$Path,[string]$ExpectedName)
    $stream=[IO.File]::OpenRead($Path); $reader=[IO.BinaryReader]::new($stream)
    try {
        if($stream.Length -lt 128 -or $reader.ReadUInt16() -ne 0x5a4d){throw 'Invalid DLL image.'}
        $stream.Position=60; $offset=$reader.ReadInt32()
        if($offset -lt 64 -or $offset -gt $stream.Length-24){throw 'Invalid DLL header.'}
        $stream.Position=$offset
        if($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664){throw 'Select an x64 DLL.'}
    } finally { $reader.Dispose(); $stream.Dispose() }
    if($ExpectedName -in @('nvngx_dlss.dll','nvngx_dlssg.dll')) {
        $version=[Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
        $original=$version.OriginalFilename
        # NVIDIA often stores a CL build number in OriginalFilename, not a DLL filename.
        if($original -like '*.dll' -and $original -ine $ExpectedName){throw "Wrong DLL type: expected $ExpectedName, found $original"}
        $description=$version.FileDescription+' '+$version.ProductName
        if(($ExpectedName -eq 'nvngx_dlss.dll' -and $description -match 'DLSS-G|DLSSNR|DLSS-RR') -or
           ($ExpectedName -eq 'nvngx_dlssg.dll' -and $description -match 'DLSSNR|SuperSampling|DLSS-RR')){throw "Wrong DLL type: $description"}
    }
}
function Get-D18SwapperDll {
    param($Entry,[string]$Name,[string]$Cache)
    $url=[uri][string]$Entry.download_url
    if($url.Scheme -ne 'https' -or $url.Host -ne 'dlss-swapper-downloads.beeradmoore.com'){throw 'Unexpected Runtime download source.'}
    $dir=Join-Path $Cache ([string]$Entry.md5_hash); $null=New-Item -ItemType Directory -Path $dir -Force
    $dll=Join-Path $dir $Name
    if((Test-Path -LiteralPath $dll) -and (Get-FileHash -LiteralPath $dll -Algorithm MD5).Hash -ieq $Entry.md5_hash){
        Assert-D18DependencyDll $dll $Name
        $cachedSignature=Get-AuthenticodeSignature -LiteralPath $dll
        if($cachedSignature.Status -ne 'Valid' -or $cachedSignature.SignerCertificate.Subject -notmatch 'NVIDIA'){throw 'Cached NVIDIA Runtime signature validation failed.'}
        return $dll
    }
    $zip=Join-Path $dir 'download.zip'; Get-D18RemoteFile $url.AbsoluteUri $zip
    if((Get-FileHash -LiteralPath $zip -Algorithm MD5).Hash -ine $Entry.zip_md5_hash){throw 'Download checksum mismatch.'}
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $entries=@($archive.Entries | Where-Object {$_.Name -ieq $Name})
        if($entries.Count -ne 1){throw "Archive does not contain one $Name"}
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0],$dll,$true)
    } finally {$archive.Dispose()}
    if((Get-FileHash -LiteralPath $dll -Algorithm MD5).Hash -ine $Entry.md5_hash){throw 'Runtime checksum mismatch.'}
    Assert-D18DependencyDll $dll $Name
    $sig=Get-AuthenticodeSignature -LiteralPath $dll
    if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'NVIDIA'){throw 'Downloaded NVIDIA Runtime signature validation failed.'}
    return $dll
}
function Get-D18StreamlineBundle {
    param([string]$Cache)
    # Official available Streamline bundle; file preparation does not assert game compatibility.
    $tag='v2.14.1'; $assetName='streamline-sdk-v2.14.1.zip'
    $dir=Join-Path $Cache 'streamline-2.14.1'; $null=New-Item -ItemType Directory -Path $dir -Force
    $meta=Join-Path $dir 'release.json'
    if(-not(Test-Path -LiteralPath $meta)){Get-D18RemoteFile "https://api.github.com/repos/NVIDIA-RTX/Streamline/releases/tags/$tag" $meta}
    $release=Get-Content -LiteralPath $meta -Raw | ConvertFrom-Json
    $assets=@($release.assets | Where-Object name -eq $assetName)
    if($assets.Count -ne 1 -or $assets[0].digest -notmatch '^sha256:([a-fA-F0-9]{64})$'){throw 'Missing Streamline release checksum.'}
    $hash=$Matches[1]; $asset=$assets[0]; $url=[uri][string]$asset.browser_download_url
    if($url.Scheme -ne 'https' -or $url.Host -ne 'github.com' -or $url.AbsolutePath -notlike '/NVIDIA-RTX/Streamline/releases/download/*'){throw 'Unexpected Streamline source.'}
    $zip=Join-Path $dir $assetName
    if(-not(Test-Path -LiteralPath $zip) -or (Get-FileHash -LiteralPath $zip).Hash -ine $hash){Get-D18RemoteFile $url.AbsoluteUri $zip}
    if((Get-FileHash -LiteralPath $zip).Hash -ine $hash){throw 'Streamline checksum mismatch.'}
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try {
        foreach($name in @('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll')) {
            $entries=@($archive.Entries | Where-Object {$_.FullName.Replace('\','/').EndsWith('/bin/x64/'+$name) -or $_.FullName.Replace('\','/') -eq ('bin/x64/'+$name)})
            if($entries.Count -ne 1){throw "Missing Streamline production file: $name"}
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0],(Join-Path $dir $name),$true)
        }
    } finally {$archive.Dispose()}
    return $dir
}
function Add-D18DependencyItems {
    param([string]$PlanPath,$Items,[string]$Game,[string]$Stage)
    $entries=@()
    if($PlanPath){$plan=Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json;$entries=@($plan.files)}
    $allowed=@('nvngx_dlss.dll','streamline\sl.interposer.dll','streamline\sl.common.dll','streamline\sl.dlss_g.dll','streamline\sl.reflex.dll','streamline\sl.pcl.dll','streamline\nvngx_dlssg.dll')
    # Keep optional files managed by a previous GUI install across uninstall/reinstall upgrades.
    $statePath=Join-Path $Game '.dlssnr-d18-install.json'
    if(Test-Path -LiteralPath $statePath){
        $old=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        foreach($record in $old.files){
            $target=[string]$record.target_relative
            if($target -in $allowed -and $target -notin @($entries | ForEach-Object target)){
                $source=Join-Path $Game $target
                if(Test-Path -LiteralPath $source){$entries+=@{source=$source;target=$target;sha256=(Get-D18Sha256 $source)}}
            }
        }
    }
    foreach($entry in $entries){
        $target=[string]$entry.target
        if($target -notin $allowed){throw "Unsupported optional dependency destination: $target"}
        $hash=Get-D18Sha256 -LiteralPath $entry.source
        if($hash -ine $entry.sha256){throw "Optional dependency changed since selection: $target"}
        Assert-D18DependencyDll $entry.source ([IO.Path]::GetFileName($target))
        $staged=Join-Path $Stage ([guid]::NewGuid().ToString('N')+'.dll')
        Copy-Item -LiteralPath $entry.source -Destination $staged
        $Items.Add([pscustomobject]@{Source=$staged;TargetRelative=$target;ExpectedHash=$hash})
    }
}
