function Show-D18Dependencies {
 param($Owner,[hashtable]$Options,[string]$Game,[string]$Lang,[string]$SessionRoot,[string]$PreviewPath,[string]$SmokeResult,[string]$Api='None',[string]$Runtime,[string]$AutoSmokeResult)
 $originalOptions=$Options.Clone()
 if(-not $Options.ContainsKey('originalNr')){$Options.originalNr=''}
 function L($zh,$en){if($Lang -eq 'zh'){$zh}else{$en}}
 $dialog=New-Object Windows.Forms.Form
 $dialog.Text=L '安装依赖' 'Installation dependencies';$dialog.ClientSize=New-Object Drawing.Size(820,([Math]::Min(815,[Windows.Forms.Screen]::PrimaryScreen.WorkingArea.Height-80)));$dialog.AutoScroll=$true;$dialog.AutoScrollMinSize=New-Object Drawing.Size(800,815)
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
 $done=ButtonAt $dialog 629 765 166 36;$done.Text=L '保存并继续' 'Save and continue'
 $notice=LabelAt $dialog 24 754 585 53 9
 if($Options.ContainsKey('autoApi') -and $Options.autoApi -and $Options.autoApi -ne $Api){$Options.autoPlan='';$Options.autoSr=$null;$Options.autoFg=$null}
 $autoSr=New-Object Windows.Forms.CheckBox;$autoSr.SetBounds(24,520,275,28);$autoSr.Text=L '补齐插件 SR' 'Prepare plugin SR';$autoSr.Checked=($Api -in @('DX11','Vulkan'));$dialog.Controls.Add($autoSr)
 $autoFg=New-Object Windows.Forms.CheckBox;$autoFg.SetBounds(300,520,275,28);$autoFg.Text=L '补齐插件 FG 配套文件' 'Prepare plugin FG bundle';$autoFg.Checked=($Api -eq 'DX11');$dialog.Controls.Add($autoFg)
 $autoVc=New-Object Windows.Forms.CheckBox;$autoVc.SetBounds(24,552,370,28);$autoVc.Text=L '补齐缺失的 VC++（可能弹出 UAC）' 'Install missing VC++ (may show UAC)';$autoVc.Checked=$true;$dialog.Controls.Add($autoVc)
 $nrBrowse=ButtonAt $dialog 410 551 203 32;$nrBrowse.Text=L '选择本地 NR 文件' 'Select local NR DLL'
 $scan=ButtonAt $dialog 629 515 166 32;$scan.Text=L '检查缺失项' 'Check missing files'
 $prepare=ButtonAt $dialog 629 551 166 32;$prepare.Text=L '一键补齐缺失项' 'Prepare missing files'
 $autoReport=New-Object Windows.Forms.TextBox;$autoReport.SetBounds(24,594,771,146);$autoReport.Multiline=$true;$autoReport.ReadOnly=$true;$autoReport.ScrollBars='Vertical';$dialog.Controls.Add($autoReport)
 $autoReport.Text=L '补齐先写入缓存，最后安装时才写进游戏目录。NR 自动查找本地文件；无兼容来源时需手动提供。DX12 默认保留游戏自身 SR/FG 路线，可按需勾选插件依赖。' 'Files are prepared in cache; game writes happen only at final installation. NR uses a local source. DX12 keeps the game SR/FG route by default; select plugin dependencies if needed.'
 if($Options.ContainsKey('autoSr') -and $null -ne $Options.autoSr){$autoSr.Checked=[bool]$Options.autoSr};if($Options.ContainsKey('autoFg') -and $null -ne $Options.autoFg){$autoFg.Checked=[bool]$Options.autoFg}
 $state=@{job=$null;catalog=$null;action='';accepted=$false;prepared=$null;runtime=$Runtime}
 $inputControls=@($srMode,$fgMode,$srVersions,$fgVersions,$srPath,$fgPath,$srBrowse,$fgBrowse,$refMode,$refPath,$refBrowse,$refConfirm,$reForce,$autoSr,$autoFg,$autoVc,$nrBrowse)

 $refresh={
  $srVersions.Enabled=($srMode.SelectedIndex -eq 1);$srPath.Enabled=($srMode.SelectedIndex -eq 2);$srBrowse.Enabled=$srPath.Enabled
  $fgVersions.Enabled=($fgMode.SelectedIndex -eq 1);$fgPath.Enabled=($fgMode.SelectedIndex -eq 2);$fgBrowse.Enabled=$fgPath.Enabled
  foreach($c in @($refMode,$refPath,$refBrowse,$refConfirm)){$c.Enabled=$reForce.Checked}
  $reForce.Enabled=-not $profile.IsRE
 }
 $srMode.Add_SelectedIndexChanged($refresh);$fgMode.Add_SelectedIndexChanged($refresh);$reForce.Add_CheckedChanged($refresh); & $refresh
 $srBrowse.Add_Click({$d=New-Object Windows.Forms.OpenFileDialog;$d.Filter='DLSS SR|*.dll';if($d.ShowDialog($dialog) -eq 'OK'){$srPath.Text=$d.FileName};$d.Dispose()})
 $fgBrowse.Add_Click({$d=New-Object Windows.Forms.FolderBrowserDialog;if($d.ShowDialog($dialog) -eq 'OK'){$fgPath.Text=$d.SelectedPath};$d.Dispose()})
 $refBrowse.Add_Click({$d=New-Object Windows.Forms.OpenFileDialog;$d.Filter='REFramework|dinput8.dll|DLL|*.dll';if($d.ShowDialog($dialog) -eq 'OK'){$refPath.Text=$d.FileName;$refMode.SelectedIndex=4};$d.Dispose()})
 $start={param($action)
  try{
   $request=@{action=$action;cache=$Options.cache;game=$Game;api=$Api;runtime=$state.runtime;originalNr=$Options.originalNr;includeSr=$autoSr.Checked;includeFg=$autoFg.Checked;installVc=$autoVc.Checked;srMode=@('Keep','Download','Local')[$srMode.SelectedIndex];fgMode=@('Keep','Download','Local')[$fgMode.SelectedIndex];reEngine=$reForce.Checked;ref=@('Auto','Recommended','Latest','Existing','Manual')[$refMode.SelectedIndex];refPath=$refPath.Text;refConfirm=$refConfirm.Checked}
   $state.job=Start-D18GuiOperation -Request $request -SessionRoot $SessionRoot;$state.action=$action
   foreach($c in @($fetch,$vc,$done,$scan,$prepare)+$inputControls){$c.Enabled=$false}
   $notice.Text=L '正在处理…下载可能需要几分钟。' 'Working… downloads may take a few minutes.'
  }catch{$notice.Text=$_.Exception.Message}
 }
 $nrBrowse.Add_Click({$d=New-Object Windows.Forms.OpenFileDialog;$d.Filter='DLSS-NR|*.dll';if($d.ShowDialog($dialog) -eq 'OK'){$state.runtime=$d.FileName;$state.prepared=$null;$Options.autoPlan='';$Options.preparedRuntime=$d.FileName;$Options.originalNr=$d.FileName;$autoReport.Text=(L '已选择 NR，点击一键补齐进行校验：' 'NR selected; click Prepare to validate: ')+$d.FileName};$d.Dispose()})
 $scan.Add_Click({& $start 'AuditDependencies'});$prepare.Add_Click({& $start 'PrepareDependencies'})
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
    }elseif($state.action -in @('AuditDependencies','PrepareDependencies')){
     $labels=if($Lang -eq 'zh'){@{kept='保留';ready='已准备';missing='缺失';manual='需手动处理';error='需处理';found='待校验';skipped='未选择 / 不需要';selected='手动选择'}}else{@{kept='Kept';ready='Prepared';missing='Missing';manual='Manual action';error='Needs attention';found='Pending validation';skipped='Not selected / not needed';selected='Manual source'}}
     $autoReport.Text=(@($r.data.rows | ForEach-Object {'['+$_.id+'] '+$labels[$_.status]+' — '+$(if($Lang -eq 'zh'){$_.zh}else{$_.en})}) -join "`r`n")
     if($state.action -eq 'PrepareDependencies'){
      $state.prepared=$r.data
      if($r.data.runtime){$state.runtime=$r.data.runtime}
     }
     $notice.Text=if($r.data.ready){L '自动准备完成，请继续最终文件检查。' 'Automatic preparation complete; continue to the final file check.'}else{L '检查已完成，请处理列表中的缺失项；已下载文件留在缓存。' 'Check complete. Resolve the listed items; prepared files remain cached.'}
    }else{$notice.Text=if($r.data.reboot){L '运行库已安装，请重启 Windows。' 'Runtime installed. Restart Windows.'}else{L '运行库安装完成。' 'Runtime installation completed.'}}
   }catch{$notice.Text=(L '未完成，可重试或使用本地文件：' 'Not completed; retry or use local files: ')+$_.Exception.Message}
   $state.job.process.Dispose();$state.job=$null;foreach($c in @($fetch,$done,$scan,$prepare)+$inputControls){$c.Enabled=$true};& $refresh
   $vc.Enabled=((Get-D18SystemDependencies -Api 'None').vc_missing.Count -gt 0)
  }
 })
 $done.Add_Click({
  if(($srMode.SelectedIndex -eq 1 -and -not $srVersions.SelectedItem) -or ($fgMode.SelectedIndex -eq 1 -and -not $fgVersions.SelectedItem)){$notice.Text=L '请获取列表并选择下载版本，或改用本地文件。' 'Load the list and choose a version, or use local files.';return}
  $Options.srMode=@('Keep','Download','Local')[$srMode.SelectedIndex];$Options.fgMode=@('Keep','Download','Local')[$fgMode.SelectedIndex]
  $Options.srPath=$srPath.Text;$Options.fgPath=$fgPath.Text;$Options.srEntry=$srVersions.SelectedItem;$Options.fgEntry=$fgVersions.SelectedItem
  $Options.ref=@('Auto','Recommended','Latest','Existing','Manual')[$refMode.SelectedIndex];$Options.refPath=$(if($refMode.SelectedIndex -eq 4){$refPath.Text}else{''});$Options.refConfirm=$refConfirm.Checked;$Options.reEngine=$reForce.Checked
  $Options.autoApi=$Api;$Options.autoSr=$autoSr.Checked;$Options.autoFg=$autoFg.Checked
  if($state.prepared){
   $Options.autoPlan=$state.prepared.plan
   if($state.prepared.runtime){$Options.preparedRuntime=$state.prepared.runtime};if(-not $Options.originalNr -and $state.prepared.nr_source){$Options.originalNr=$state.prepared.nr_source}
   if($state.prepared.ref_path -and -not $Options.refPath -and $Options.ref -in @('Auto','Recommended')){$Options.ref='Manual';$Options.refPath=$state.prepared.ref_path}
  }
  $state.accepted=$true;$dialog.Close()
 })
 $invalidate={$state.prepared=$null;$Options.autoPlan=''}
 foreach($c in @($autoSr,$autoFg,$autoVc,$reForce)){$c.Add_CheckedChanged($invalidate)}
 $dialog.Add_FormClosing({param($s,$e) if($state.job){$e.Cancel=$true}})
 if($AutoSmokeResult){
  # Exercise the real WinForms callbacks against the real worker, with no game installation.
  $dialog.Show();$timer.Start();$scan.PerformClick();$deadline=[DateTime]::UtcNow.AddSeconds(60)
  while($state.job -and [DateTime]::UtcNow -lt $deadline){[Windows.Forms.Application]::DoEvents();[Threading.Thread]::Sleep(20)}
  if($state.job -or -not $autoReport.Text.Contains('[NR]')){throw 'Automatic dependency UI audit did not complete.'}
  $prepare.PerformClick();$deadline=[DateTime]::UtcNow.AddSeconds(90)
  while($state.job -and [DateTime]::UtcNow -lt $deadline){[Windows.Forms.Application]::DoEvents();[Threading.Thread]::Sleep(20)}
  if($state.job -or -not $state.prepared -or -not $state.prepared.runtime){throw ('Automatic dependency UI preparation failed: '+$autoReport.Text+' '+$notice.Text)}
  if(-not $done.Enabled -or -not $prepare.Enabled){throw 'Dependency UI stayed disabled after completion.'}
  $done.PerformClick()
  if(-not $state.accepted -or -not $Options.autoPlan -or -not $Options.preparedRuntime){throw 'Prepared dependency selections were not saved.'}
  @{pass=$true;audit_callback=$true;prepare_callback=$true;save_callback=$true;runtime=$Options.preparedRuntime;plan=$Options.autoPlan}|ConvertTo-Json|Set-Content -LiteralPath $AutoSmokeResult -Encoding UTF8
 }
 elseif($SmokeResult){
  $dialog.Show();$timer.Start();$fetch.PerformClick();$deadline=[DateTime]::UtcNow.AddSeconds(45)
  while($state.job -and [DateTime]::UtcNow -lt $deadline){[Windows.Forms.Application]::DoEvents();[Threading.Thread]::Sleep(20)}
  if($srVersions.Items.Count -eq 0 -or $fgVersions.Items.Count -eq 0){throw ('Version lists were not populated: '+$notice.Text)}
  $srMode.SelectedIndex=1;$fgMode.SelectedIndex=1;$srVersions.SelectedIndex=0;$fgVersions.SelectedIndex=0;$done.PerformClick()
  if(-not $state.accepted -or $Options.srMode -ne 'Download' -or -not $Options.fgEntry){throw 'Dependency choices were not saved.'}
  @{catalog=$true;selected_sr=$Options.srEntry.version;selected_fg=$Options.fgEntry.version}|ConvertTo-Json|Set-Content -LiteralPath $SmokeResult -Encoding UTF8
 }
 elseif($PreviewPath){$dialog.Show();[Windows.Forms.Application]::DoEvents();$b=New-Object Drawing.Bitmap($dialog.Width,$dialog.Height);$dialog.DrawToBitmap($b,(New-Object Drawing.Rectangle(0,0,$dialog.Width,$dialog.Height)));$b.Save($PreviewPath);$b.Dispose();$dialog.Close()}
 else{$timer.Start();$null=$dialog.ShowDialog($Owner)}
 if(-not $state.accepted){$Options.Clear();foreach($key in $originalOptions.Keys){$Options[$key]=$originalOptions[$key]}}
 $timer.Dispose();$dialog.Dispose();return $state.accepted
}
