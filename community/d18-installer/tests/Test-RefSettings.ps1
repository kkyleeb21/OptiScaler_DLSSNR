#Requires -Version 5.1
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\Onimusha-Profile.ps1')
$original="REFrameworkConfig_MenuKey_V2=45`r`nOther=keep`r`n"
$fields=@{REFrameworkConfig_MenuKey_V2='34'}
$record=$fields|ConvertTo-Json|ConvertFrom-Json
$installed=Set-D18RefFields $original $fields
if($installed -notmatch 'MenuKey_V2=34' -or $installed -notmatch 'Other=keep'){throw 'Set failed'}
if((Restore-D18RefFields $installed $original $record) -ne $original){throw 'Restore failed'}
Write-Output 'PASS unchanged key restored'
$edited=$installed.Replace('=34','=35').Replace('Other=keep','Other=user')
if((Restore-D18RefFields $edited $original $record) -ne $edited){throw 'User edit lost'}
Write-Output 'PASS user key and unrelated edits preserved'
$edited=$installed.Replace('Other=keep','Other=user')
$restored=Restore-D18RefFields $edited $original $record
if($restored -notmatch 'MenuKey_V2=45' -or $restored -notmatch 'Other=user'){throw 'Field-only restore failed'}
Write-Output 'PASS unchanged key restored alongside unrelated user edit'
$new=Set-D18RefFields '' $fields
if(-not [string]::IsNullOrWhiteSpace((Restore-D18RefFields $new '' $record))){throw 'New field not removed'}
Write-Output 'PASS newly added field removed'
if((Restore-D18RefFields 'Other=user' $original $record) -ne 'Other=user'){throw 'User deletion not preserved'}
Write-Output 'PASS user deletion preserved'
$hide=@{REFrameworkConfig_MenuOpen='false';REFrameworkConfig_RememberMenuState='true'}
$hidden=Set-D18RefFields $original $hide
if($hidden -notmatch 'MenuKey_V2=45' -or $hidden -notmatch 'MenuOpen=false'){throw 'Hide coupled to hotkey'}
Write-Output 'PASS hide independent of hotkey'
