$ErrorActionPreference='Stop'
$repo='E:\DLSSNR\workspace\dlss5\worktrees\d18-011-onimusha-release'
$installer=Join-Path $repo 'community\d18-installer'
$baseline='E:\DLSSNR\releases\D18_0.1.1_Onimusha_ONLY_FlickerFix'
$output='E:\DLSSNR\releases\D18_0.1.1_Onimusha_ONLY_FinalUpdate_20260906'
$core='E:\DLSSNR\builds\D18ReconstructionABHotkeys-20260906\OptiScaler.dll'
. (Join-Path $installer 'D18-Common.ps1')
$null=Test-D18Payload -PayloadRoot "$baseline\payload" -ManifestPath "$baseline\payload_manifest.json"
if((Get-D18Sha256 $core) -ne '8A062E2995D9C098049D6D441324B710420659EBBF5C496D2A29002B3000F7CF'){throw 'Unexpected core'}
if((Test-Path $output) -or (Test-Path "$output.zip")){throw 'Output exists'}
Copy-Item -LiteralPath $baseline -Destination $output -Recurse
foreach($file in @('D18-Common.ps1','Install-D18.ps1','Install-D18.bat','Uninstall-D18.ps1','Uninstall-D18.bat','Onimusha-Profile.ps1','ONIMUSHA_README.md','ONIMUSHA_AB_RESULTS_CN.md','README.md','README_CN.md','THIRD_PARTY_NOTICES.md','VERSION')) {
    Copy-Item -LiteralPath (Join-Path $installer $file) -Destination $output -Force
}
Copy-Item -LiteralPath $core -Destination "$output\payload\OptiScaler.dll" -Force
Copy-Item -LiteralPath "$installer\Onimusha-default.ini" -Destination "$output\payload\OptiScaler.ini.d18" -Force
if(Get-ChildItem -LiteralPath $output -Recurse -Filter nvngx_dlssnr.dll){throw 'Runtime must not be bundled'}
$commit=(& git -C $repo rev-parse HEAD).Trim()
if($commit -ne '678af8e8cf15c71e7687ff4656b8ebad8026adc2'){throw 'Unexpected source revision'}
$payload=Join-Path $output 'payload'
$entries=@(Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=$_.FullName.Substring($payload.Length+1);size=$_.Length;sha256=(Get-D18Sha256 $_.FullName)}
})
$manifest=[ordered]@{format='dlssnr-d18-payload-manifest-v1';release_name='D18 0.1.1 - Onimusha ONLY Final Update 20260906';release_version='0.1.1';game_profile='onimusha-only';contains_nvidia_runtime=$false;source_commit=$commit;files=$entries}
[IO.File]::WriteAllText("$output\payload_manifest.json",($manifest|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
$null=Test-D18Payload -PayloadRoot $payload -ManifestPath "$output\payload_manifest.json"
$sums=@(Get-ChildItem -LiteralPath $output -Recurse -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object FullName | ForEach-Object { (Get-D18Sha256 $_.FullName)+'  '+$_.FullName.Substring($output.Length+1) })
[IO.File]::WriteAllLines("$output\SHA256SUMS.txt",$sums)
Compress-Archive -LiteralPath $output -DestinationPath "$output.zip"
Get-FileHash -LiteralPath "$output.zip"
Get-Item -LiteralPath "$output.zip" | Select-Object FullName,Length
