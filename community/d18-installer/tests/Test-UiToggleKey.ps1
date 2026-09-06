$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\D18-Common.ps1')
foreach ($pair in @(@('',45),@('Insert',45),@('f10',121),@('Home',36),@('F24',135),@('A',65),@('0',48))) {
    if ((ConvertTo-D18UiKey $pair[0]) -ne $pair[1]) { throw "Key parse failed: $pair" }
}
foreach ($invalid in @('Ctrl+F10','F25','-1','0x01','nonsense')) {
    $failed=$false
    try { $null=ConvertTo-D18UiKey $invalid } catch { $failed=$true }
    if(-not $failed){throw "Invalid key accepted: $invalid"}
}
$text="[Other]`r`nShortcutKey=77`r`n[Menu]`r`nScale=1.7`r`nShortcutKey=auto`r`n[DlssNr]`r`nToggleKey=123`r`n"
$updated=Set-D18UiKey $text '121'
if((Get-D18UiKey (Set-D18UiKey '[Menu]' '45')) -ne '45'){throw 'EOF section failed'}
if((Get-D18UiKey $updated) -ne '121' -or $updated -notmatch 'ShortcutKey=77' -or $updated -notmatch 'ToggleKey=123' -or $updated -notmatch 'Scale=1.7'){throw 'INI preservation failed'}
if((Get-D18UiKey (Set-D18UiKey "[Other]`nKeep=1" '45')) -ne '45'){throw 'Missing section insertion failed'}
if((Get-D18UiKey (Set-D18UiKey "[Menu]`nScale=1`n[Other]`nKeep=1" '36')) -ne '36'){throw 'Missing key insertion failed'}
foreach($text in @("[Menu]`nShortcutKey=1`nShortcutKey=2", "[Menu]`nShortcutKey=1`n[Menu]`nShortcutKey=2")) {
    $failed=$false
    try{$null=Set-D18UiKey $text '45'}catch{$failed=$true}
    if(-not $failed){throw 'Duplicate accepted'}
}
$tokens=$null; $errors=$null
$null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '..\Install-D18.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors | Out-String)}
Write-Output 'PASS UI key parsing, invalid input, INI preservation, missing sections, duplicates and installer syntax'
