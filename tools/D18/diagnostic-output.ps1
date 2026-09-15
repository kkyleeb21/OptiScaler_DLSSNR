# Shared user-facing output policy. No game writes or source deletion.
function New-D18DiagnosticDirectory {
    [CmdletBinding()]
    param([string]$OutputDirectory, [string[]]$SourceDirectories)
    if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
        $base = [Environment]::GetFolderPath('LocalApplicationData')
        if ([string]::IsNullOrWhiteSpace($base)) { $base = [IO.Path]::GetTempPath() }
        $name = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
        $OutputDirectory = Join-Path (Join-Path $base 'DLSSNR\Diagnostics') $name
    }
    $out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
    $out = [IO.Path]::GetFullPath($out).TrimEnd('\','/')
    foreach ($source in $SourceDirectories) {
        if ([string]::IsNullOrWhiteSpace($source)) { continue }
        $src = (Resolve-Path -LiteralPath $source -ErrorAction Stop).ProviderPath.TrimEnd('\','/')
        if ($out.Equals($src,[StringComparison]::OrdinalIgnoreCase) -or $out.StartsWith($src+'\',[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Choose an output directory outside the game and source capture directories.'
        }
    }
    if (Test-Path -LiteralPath $out) { throw 'OutputDirectory already exists; choose a new directory to preserve previous evidence.' }
    # Reject junction/symlink ancestors so an apparent external path cannot write into a source.
    $parent = [IO.Path]::GetDirectoryName($out)
    while ($parent) {
        if (Test-Path -LiteralPath $parent) {
            $item = Get-Item -LiteralPath $parent -Force -ErrorAction Stop
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'OutputDirectory must not traverse a junction or symbolic link.' }
        }
        $parent = [IO.Path]::GetDirectoryName($parent)
    }
    [IO.Directory]::CreateDirectory($out) | Out-Null
    return $out
}
