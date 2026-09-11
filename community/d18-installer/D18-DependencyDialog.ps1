function Show-D18Dependencies {
 param($Owner,[hashtable]$Options,[string]$Game,[string]$Lang,[string]$SessionRoot,[string]$PreviewPath,[string]$SmokeResult)
 function L($zh,$en){if($Lang -eq 'zh'){$zh}else{$en}}
 $dialog=New-Object Windows.Forms.Form
 $dialog.Text=L '安装依赖' 'Installation dependencies';$dialog.ClientSize=New-Object Drawing.Size(820,600)
 $dialog.Font=New-Object Drawing.Font('Microsoft YaHei UI',10);$dialog.StartPosition='CenterParent';$dialog.FormBorderStyle='FixedDialog';$dialog.MaximizeBox=$false
 $heading=LabelAt $dialog 24 20 770 32 16;$heading.Text=L '按需安装，默认保留已有文件' 'Optional installs. Existing files are kept by default.'
 $srLabel=LabelAt $dialog 24 72 100 28;$srLabel.Text='DLSS SR'
 $fgLabel=LabelAt $dialog 24 160 100 28;$fgLabel.Text=L '插件 FG' 'Plugin FG'
 $modes=@((L '保留已有' 'Keep existing'),(L '下载所选版本' 'Download version'),(L '选择本地文件' 'Use local files'))
 $srMode=ComboAt $dialog 140 68 180 $modes;$srMode.SelectedIndex=[Array]::IndexOf(@('Keep','Download','Local'),$Options.srMode)
 $fgMode=ComboAt $dialog 140 156 180 $modes;$fgMode.SelectedIndex=[Array]::IndexOf(@('Keep','Download','Local'),$Options.fgMode)
 $srVersions=ComboAt $dialog 338 68 275 @();$fgVersions=ComboAt $dialog 338 156 275 @()
 $srVersions.DisplayMember='version';$fgVersions.DisplayMember='version'
 if($Options.srEntry){$null=$srVersions.Items.Add($Options.srEntry);$srVersions.SelectedIndex=0}
 if($Options.fgEntry){$null=$fgVersions.Items.Add($Options.fgEntry);$fgVersions.SelectedIndex=0}
 $fetch=ButtonAt $dialog 629 68 166 32;$fetch.Text=L '获取版本列表' 'Load versions'
 $srPath=TextAt $dialog 140 109 473;$srPath.Text=$Options.srPath
 $fgPath=TextAt $dialog 140 197 473;$fgPath.Text=$Options.fgPath
 $srBrowse=ButtonAt $dialog 629 108 166 32;$srBrowse.Text=L '选择 SR DLL' 'Select SR DLL'
 $fgBrowse=ButtonAt $dialog 629 196 166 32;$fgBrowse.Text=L '选择 FG 文件夹' 'Select FG folder'
 $fgHint=LabelAt $dialog 24 239 770 40 9;$fgHint.Text=L 'FG 下载会补齐 Streamline 配套文件；这里只安装文件，功能仍在游戏内启用。' 'FG downloads include Streamline companions. Enable the feature in the game after installing.'
 $reForce=New-Object Windows.Forms.CheckBox;$reForce.SetBounds(24,285,760,28);$reForce.Text=L '这是 RE Engine 游戏（已知游戏自动识别，未知游戏可勾选）' 'RE Engine game (known games detected; select for an unlisted game)';$dialog.Controls.Add($reForce)
 $profile=Get-D18ReProfile -Game $Game;$reForce.Checked=($profile.IsRE -or $Options.reEngine)
 if($profile.IsRE){$reForce.Enabled=$false}
 $refMode=ComboAt $dialog 24 325 270 @((L '自动处理 REF' 'Automatic REF'),(L '下载匹配版本' 'Download matched build'),(L '下载最新版本' 'Download latest build'),(L '使用已有 REF' 'Use existing REF'),(L '本地选择 / 稍后准备' 'Local file / prepare later'))
 $refMode.SelectedIndex=[Math]::Max(0,[Array]::IndexOf(@('Auto','Recommended','Latest','Existing','Manual'),$Options.ref))
 $refPath=TextAt $dialog 311 325 300;$refPath.Text=$Options.refPath
 $refBrowse=ButtonAt $dialog 629 324 166 32;$refBrowse.Text=L '选择 dinput8.dll' 'Select dinput8.dll'
 $refConfirm=New-Object Windows.Forms.CheckBox;$refConfirm.SetBounds(24,366,770,49);$refConfirm.Text=L '若目录已有其他版本的 dinput8.dll，我确认它是 REFramework，可复用或替换。' 'If a different dinput8.dll exists, I confirm it is REFramework and may be reused or replaced.';$refConfirm.Checked=$Options.refConfirm;$dialog.Controls.Add($refConfirm)
 $refHint=LabelAt $dialog 24 416 770 40 9;$refHint.Text=L 'REF 放在游戏 EXE 旁，菜单键 PgDn；没有匹配记录可选最新或本地文件。' 'REF is placed beside the game EXE; menu key: PgDn. Use latest or a local file when no matched build exists.'
 $sys=Get-D18SystemDependencies -Api 'None'
 $health=LabelAt $dialog 24 470 570 48 9
 $health.Text=if($sys.vc_missing.Count){(L 'VC++ x64 缺少：' 'VC++ x64 missing: ')+($sys.vc_missing -join ', ')}else{L 'VC++ x64 基础依赖已找到。' 'VC++ x64 base dependencies found.'}
 $vc=ButtonAt $dialog 629 465 166 36;$vc.Text=L '安装 VC++ x64' 'Install VC++ x64';$vc.Enabled=($sys.vc_missing.Count -gt 0)
 $done=ButtonAt $dialog 629 545 166 36;$done.Text=L '保存并继续' 'Save and continue'
 $notice=LabelAt $dialog 24 530 585 53 9
 $state=@{job=$null;catalog=$null;action='';accepted=$false}
 $refresh={
  $srVersions.Enabled=($srMode.SelectedIndex -eq 1);$srPath.Enabled=($srMode.SelectedIndex -eq 2);$srBrowse.Enabled=$srPath.Enabled
  $fgVersions.Enabled=($fgMode.SelectedIndex -eq 1);$fgPath.Enabled=($fgMode.SelectedIndex -eq 2);$fgBrowse.Enabled=$fgPath.Enabled
  foreach($c in @($refMode,$refPath,$refBrowse,$refConfirm)){$c.Enabled=$reForce.Checked}
 }
 $srMode.Add_SelectedIndexChanged($refresh);$fgMode.Add_SelectedIndexChanged($refresh);$reForce.Add_CheckedChanged($refresh); & $refresh
 $srBrowse.Add_Click({$d=New-Object Windows.Forms.OpenFileDialog;$d.Filter='DLSS SR|*.dll';if($d.ShowDialog($dialog) -eq 'OK'){$srPath.Text=$d.FileName};$d.Dispose()})
 $fgBrowse.Add_Click({$d=New-Object Windows.Forms.FolderBrowserDialog;if($d.ShowDialog($dialog) -eq 'OK'){$fgPath.Text=$d.SelectedPath};$d.Dispose()})
 $refBrowse.Add_Click({$d=New-Object Windows.Forms.OpenFileDialog;$d.Filter='REFramework|dinput8.dll|DLL|*.dll';if($d.ShowDialog($dialog) -eq 'OK'){$refPath.Text=$d.FileName;$refMode.SelectedIndex=4};$d.Dispose()})
 $start={param($action)
  try{$state.job=Start-D18GuiOperation -Request @{action=$action;cache=$Options.cache} -SessionRoot $SessionRoot;$state.action=$action;$fetch.Enabled=$false;$vc.Enabled=$false;$done.Enabled=$false;$notice.Text=L '正在处理…' 'Working…'}catch{$notice.Text=$_.Exception.Message}
 }
 $fetch.Add_Click({& $start 'Catalog'});$vc.Add_Click({& $start 'InstallVC'})
 $timer=New-Object Windows.Forms.Timer;$timer.Interval=250
 $timer.Add_Tick({
  if($state.job -and $state.job.process.HasExited){
   try{
    $r=Get-Content -LiteralPath $state.job.result -Raw -Encoding UTF8|ConvertFrom-Json
    if(-not $r.success){throw $r.message}
    if($state.action -eq 'Catalog'){
     $state.catalog=$r.data
     foreach($pair in @(@($srVersions,@($r.data.dlss)),@($fgVersions,@($r.data.dlss_g)))){
      $box=$pair[0];$box.Items.Clear();$box.DisplayMember='version'
      foreach($e in @($pair[1]|Where-Object {-not $_.is_dev_file -and $_.is_signature_valid}|Sort-Object version_number -Descending)){$null=$box.Items.Add($e)}
     }
     $notice.Text=L '列表已更新，请选择要安装的版本。' 'List updated. Select a version to install.'
    }else{$notice.Text=if($r.data.reboot){L '运行库已安装，请重启 Windows。' 'Runtime installed. Restart Windows.'}else{L '运行库安装完成。' 'Runtime installation completed.'}}
   }catch{$notice.Text=(L '未完成，可重试或使用本地文件：' 'Not completed; retry or use local files: ')+$_.Exception.Message}
   $state.job.process.Dispose();$state.job=$null;$fetch.Enabled=$true;$done.Enabled=$true
   $vc.Enabled=((Get-D18SystemDependencies -Api 'None').vc_missing.Count -gt 0)
  }
 })
 $done.Add_Click({
  if(($srMode.SelectedIndex -eq 1 -and -not $srVersions.SelectedItem) -or ($fgMode.SelectedIndex -eq 1 -and -not $fgVersions.SelectedItem)){$notice.Text=L '请获取列表并选择下载版本，或改用本地文件。' 'Load the list and choose a version, or use local files.';return}
  $Options.srMode=@('Keep','Download','Local')[$srMode.SelectedIndex];$Options.fgMode=@('Keep','Download','Local')[$fgMode.SelectedIndex]
  $Options.srPath=$srPath.Text;$Options.fgPath=$fgPath.Text;$Options.srEntry=$srVersions.SelectedItem;$Options.fgEntry=$fgVersions.SelectedItem
  $Options.ref=@('Auto','Recommended','Latest','Existing','Manual')[$refMode.SelectedIndex];$Options.refPath=$(if($refMode.SelectedIndex -eq 4){$refPath.Text}else{''});$Options.refConfirm=$refConfirm.Checked;$Options.reEngine=$reForce.Checked
  $state.accepted=$true;$dialog.Close()
 })
 $dialog.Add_FormClosing({param($s,$e) if($state.job){$e.Cancel=$true}})
 if($SmokeResult){
  $dialog.Show();$timer.Start();$fetch.PerformClick();$deadline=[DateTime]::UtcNow.AddSeconds(45)
  while($state.job -and [DateTime]::UtcNow -lt $deadline){[Windows.Forms.Application]::DoEvents();[Threading.Thread]::Sleep(20)}
  if($srVersions.Items.Count -eq 0 -or $fgVersions.Items.Count -eq 0){throw ('Version lists were not populated: '+$notice.Text)}
  $srMode.SelectedIndex=1;$fgMode.SelectedIndex=1;$srVersions.SelectedIndex=0;$fgVersions.SelectedIndex=0;$done.PerformClick()
  if(-not $state.accepted -or $Options.srMode -ne 'Download' -or -not $Options.fgEntry){throw 'Dependency choices were not saved.'}
  @{catalog=$true;selected_sr=$Options.srEntry.version;selected_fg=$Options.fgEntry.version}|ConvertTo-Json|Set-Content -LiteralPath $SmokeResult -Encoding UTF8
 }
 elseif($PreviewPath){$dialog.Show();[Windows.Forms.Application]::DoEvents();$b=New-Object Drawing.Bitmap($dialog.Width,$dialog.Height);$dialog.DrawToBitmap($b,(New-Object Drawing.Rectangle(0,0,$dialog.Width,$dialog.Height)));$b.Save($PreviewPath);$b.Dispose();$dialog.Close()}
 else{$timer.Start();$null=$dialog.ShowDialog($Owner)}
 $timer.Dispose();$dialog.Dispose();return $state.accepted
}
