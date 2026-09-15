[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Manifest,[switch]$Apply)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath 'E:\DLSSNR').Path.TrimEnd('\')
$plan = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
if ($plan.schema -ne 'd18-explicit-cleanup-v1') { throw 'Unsupported cleanup manifest' }
function Assert-Entry($entry) {
    $path = [IO.Path]::GetFullPath([string]$entry.path)
    if (!$path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Outside workspace: $path" }
    $relative = $path.Substring($root.Length + 1)
    $cache = $entry.role -eq 'rebuildable_compiler_cache' -and $relative -match '^builds\\[^\\]+\\(core-obj|intermediate)\\' -and [IO.Path]::GetExtension($path) -in @('.obj','.pch','.iobj','.ipdb')
    $private = $entry.role -eq 'user_retired_private_package' -and $relative -eq 'releases\DLSSNR_D18_0.1.5.zip' -and $plan.authorization -eq 'User explicitly retired 0.1.5; reviewed render compiler caches only'
    if (!$cache -and !$private) { throw "Protected or unsupported cleanup role: $path" }
    if ($relative -match '(?i)^builds\\[^\\]*(wildlands|dx11|postreplay)[^\\]*\\') { throw "Parallel task protected: $path" }
    $cursor = Get-Item -LiteralPath $path -Force
    while ($cursor.FullName -ne $root) {
        if ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point rejected: $($cursor.FullName)" }
        $cursor = Get-Item -LiteralPath ([IO.Path]::GetDirectoryName($cursor.FullName)) -Force
    }
    $item = Get-Item -LiteralPath $path -Force
    if ($item.PSIsContainer -or $item.Length -ne [long]$entry.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Identity changed: $path" }
    return $path
}
# Validate every file before removing any. Latest release and all evidence/backups are outside the allowlist.
foreach ($protected in @($plan.protected_release) + @($plan.protected_products)) {
    if (!$protected -or !(Test-Path -LiteralPath $protected.path -PathType Leaf) -or (Get-FileHash -LiteralPath $protected.path -Algorithm SHA256).Hash -ne $protected.sha256) { throw 'Protected release or rollback product failed verification' }
}
$seen = @{}
foreach ($entry in $plan.files) { $path = Assert-Entry $entry; if ($seen.ContainsKey($path)) { throw 'Duplicate entry' }; $seen[$path] = $true }
$total = ($plan.files | Measure-Object -Property bytes -Sum).Sum
if (!$Apply) { [pscustomobject]@{mode='dry-run';files=$seen.Count;logicalBytes=$total}; return }
$manifestPath = (Resolve-Path -LiteralPath $Manifest).Path
if (!$manifestPath.StartsWith($root + '\reports\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Applied manifests must be preserved in project reports' }
$receipt = $manifestPath + '.receipt.jsonl'
if (Test-Path -LiteralPath $receipt) { throw 'Existing receipt: inspect before reusing this manifest' }
foreach ($entry in $plan.files) {
    $path = Assert-Entry $entry
    Remove-Item -LiteralPath $path -Force
    if (Test-Path -LiteralPath $path) { throw "Removal failed: $path" }
    [pscustomobject]@{path=$path;bytes=$entry.bytes;sha256=$entry.sha256;deletedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Compress | Add-Content -LiteralPath $receipt -Encoding utf8
}
[pscustomobject]@{mode='applied';files=$seen.Count;logicalBytes=$total;receipt=$receipt}
