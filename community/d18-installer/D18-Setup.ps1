#Requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('zh','en')][string]$Language = $(if ([Globalization.CultureInfo]::CurrentUICulture.Name -like 'zh*') {'zh'} else {'en'}),
      [string]$PreviewDirectory,
      [string]$UiSmokeDirectory,
      [string]$SmokeRuntime)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
. (Join-Path $PSScriptRoot 'D18-GuiCommon.ps1')
. (Join-Path $PSScriptRoot 'D18-Common.ps1')
. (Join-Path $PSScriptRoot 'D18-REFramework.ps1')
. (Join-Path $PSScriptRoot 'D18-Dependencies.ps1')
. (Join-Path $PSScriptRoot 'D18-GameDiscovery.ps1')
. (Join-Path $PSScriptRoot 'D18-DependencyDialog.ps1')
$script:options=@{srMode='Keep';fgMode='Keep';srPath='';fgPath='';srEntry=$null;fgEntry=$null;ref='Auto';refPath='';refConfirm=$false;reEngine=$false;cache=$null}

$script:lang = $Language
$script:step = 0
$script:job = $null
$script:check = $null
$script:selectionGame = ''
$script:complete = $false
$script:lastCode = ''
$script:lastAction = ''
$script:sessionRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'D18\InstallerLogs'
$script:words = @{
 zh = @{
  subtitle='选择游戏，安装 D18'; language='语言'; steps=@('01  游戏','02  图形 API','03  DLSS5 文件','04  确认安装')
  titles=@('先选择游戏程序','选择图形 API 与加载名称','选择你的 DLSS5 文件','检查完成，准备安装')
  hints=@('选择实际运行游戏的 EXE。安装目录会跟随该文件。','请选择游戏实际使用的 API；可以在游戏设置或启动选项中查看。','选择 nvngx_dlssnr.dll。安装器会校验并准备所需补丁。','确认下列选项后开始安装。已有 D18 将升级，并保留备份。')
  browse='浏览…'; exe='游戏 EXE'; folder='安装目录'; api='图形 API'; proxy='代理 DLL 名称'; proxyHint='这是 D18 的加载文件名。下拉列表只提供安装器支持的名称。'
  ref='REFramework（RE Engine 游戏）'; refItems=@('自动处理','匹配版本','最新版本','使用已有版本','手动准备'); refHint='普通游戏忽略此项。已有配置会保留；新装菜单键默认 Insert。'
  runtime='DLSS5 / DLSS-NR 文件'; runtimeHint='文件由你提供，安装包不附带。nvngx_dlss.dll 是 SR 文件，不能代替 NR。'
  back='上一步'; next='下一步'; check='检查文件'; install='安装 / 升级'; close='关闭'; finish='完成'; installedTitle='安装完成'; removedTitle='卸载完成'; pendingTitle='D18 安装完成，REF 待准备'; installedHint='所选文件已安装并通过校验。可以点击“完成”关闭安装器，或查看安装日志。'; uninstall='卸载 D18'; details='查看日志'; hideDetails='收起日志'
  dependencies='选择安装依赖'; games='查找已安装游戏'; running='从运行进程选择'; discovering='正在查找游戏…'; pending='D18 已安装，REFramework 尚未准备。请先安装 REF 再启动游戏。'; ready='准备就绪'; checking='正在检查安装包与 Runtime…'; installing='正在安装，请保持窗口打开…'; removing='正在卸载，请保持窗口打开…'
  done='安装完成，文件已验证。可以启动游戏，按 Insert 或原有菜单键打开 D18。'; removed='卸载完成，备份已保留。'; checked='文件检查通过。安装时会再次检查，并创建可恢复备份。'
  game_exe='请选择实际存在的游戏 EXE。'; api_error='请选择游戏使用的图形 API。'; runtime_path='请选择实际存在的 DLSS-NR DLL 文件。'
  game_running='请先退出游戏，再重试。'; layout_conflict='此文件的 DX11 主机布局不兼容。请选择支持的 310.8 系 NR 文件。'
  runtime_conflict='此 Runtime 与补丁要求不匹配。请确认选择的是支持的 DLSS-NR 文件。'; reframework='REFramework 需要处理。请返回图形 API 页面调整选项；详情见日志。'
  anti_cheat='检测到反作弊组件，请阅读确认页的提示。'; access_denied='没有文件写入权限，或文件被占用。请检查目录权限和运行中的程序。'
  operation_failed='操作未完成。请查看日志中的具体原因及恢复结果。'; summary='游戏：{0}\r\n目录：{1}\r\nAPI：{2}\r\n代理名称：{3}\r\nDLSS5：{4}'
  proxyWarning='该代理名称已有文件；安装会备份它。请确认这是你希望使用的加载名称。'
  ack='检测到反作弊组件。我了解使用注入模组可能导致启动失败或账号处罚，仍要安装。'
  ackNeeded='需要先勾选上方的反作弊风险确认，或返回更换游戏。'; uninstallConfirm='卸载所选游戏目录中的 D18？\r\n\r\n{0}\r\n\r\n将调用通用卸载器，保留备份。'
  busyClose='任务仍在执行，请等待完成后关闭。'; preview='预览：仅演示界面，尚未校验文件。'; logPath='日志目录：'; checkingTitle='正在检查文件'; successTitle='已完成'; errorTitle='需要处理'; reviewWarning='静态检查通过不代表所有显卡或游戏均兼容。'
 }
 en = @{
  subtitle='Choose a game. Install D18.'; language='Language'; steps=@('01  Game','02  Graphics API','03  DLSS5 file','04  Install')
  titles=@('Choose your game executable','Choose the API and loader name','Select your DLSS5 file','Checked and ready to install')
  hints=@('Select the EXE that runs the game. Its folder becomes the install location.','Use the API selected in the game settings or launch options.','Select nvngx_dlssnr.dll. Setup will check the file and prepare its patches.','Review your choices. An existing D18 installation will be upgraded with a backup.')
  browse='Browse…'; exe='Game EXE'; folder='Install folder'; api='Graphics API'; proxy='Proxy DLL name'; proxyHint='This is the filename used to load D18. Only supported names are listed.'
  ref='REFramework (RE Engine games)'; refItems=@('Automatic','Matched version','Latest version','Use existing','Prepare manually'); refHint='Ignored for other games. Existing settings stay; new installs use Insert for the menu.'
  runtime='DLSS5 / DLSS-NR file'; runtimeHint='Supply your own file; it is not bundled. nvngx_dlss.dll is SR, and cannot replace NR.'
  back='Back'; next='Next'; check='Check files'; install='Install / Upgrade'; close='Close'; finish='Finish'; installedTitle='Installation complete'; removedTitle='Uninstall complete'; pendingTitle='D18 installed — REF still required'; installedHint='The selected files have been installed and verified. Select Finish to close setup, or view the installation log.'; uninstall='Uninstall D18'; details='View log'; hideDetails='Hide log'
  dependencies='Choose dependencies'; games='Find installed games'; running='Select running process'; discovering='Finding games…'; pending='D18 installed. REFramework is still missing; prepare it before launching the game.'; ready='Ready'; checking='Checking package and Runtime…'; installing='Installing. Keep this window open…'; removing='Uninstalling. Keep this window open…'
  done='Installed and verified. Launch your game and press Insert or your saved menu key.'; removed='Uninstalled. Backups have been retained.'; checked='File checks passed. Installation will recheck and create a recoverable backup.'
  game_exe='Select an existing game EXE.'; api_error='Choose the graphics API used by the game.'; runtime_path='Select an existing DLSS-NR DLL file.'
  game_running='Close the game, then try again.'; layout_conflict='This file has an incompatible DX11 host layout. Select a supported 310.8-based NR file.'
  runtime_conflict='The Runtime does not match the patch requirements. Select a supported DLSS-NR file.'; reframework='REFramework needs attention. Go back to the API page and adjust its option; see the log.'
  anti_cheat='Anti-cheat components were detected. Read the notice on the review page.'; access_denied='Access denied or a file is in use. Check folder permissions and running applications.'
  operation_failed='The operation did not complete. See the log for the cause and recovery outcome.'; summary='Game: {0}\r\nFolder: {1}\r\nAPI: {2}\r\nProxy name: {3}\r\nDLSS5: {4}'
  proxyWarning='A file already uses this proxy name. Setup will back it up. Confirm this is your intended loader name.'
  ack='Anti-cheat detected. I understand injection mods may cause launch failures or account penalties and still want to install.'
  ackNeeded='Acknowledge the anti-cheat notice above, or go back and choose another game.'; uninstallConfirm='Uninstall D18 from the selected game folder?\r\n\r\n{0}\r\n\r\nThe shared uninstaller will retain backups.'
  busyClose='An operation is running. Wait for it to finish before closing.'; preview='Preview only: no files have been checked.'; logPath='Log folder: '; checkingTitle='Checking files'; successTitle='Completed'; errorTitle='Needs attention'; reviewWarning='Static checks do not prove compatibility with every GPU or game.'
 }
}
function T([string]$key) { return ([string]$script:words[$script:lang][$key]).Replace('\r\n',"`r`n") }
$form = New-Object Windows.Forms.Form
$form.Text = 'D18 Setup — Preview'
$form.ClientSize = New-Object Drawing.Size(900,660)
$form.MinimumSize = New-Object Drawing.Size(916,699)
$form.FormBorderStyle = 'FixedSingle'
$form.MaximizeBox = $false
$form.StartPosition = 'CenterScreen'
$form.AutoScaleMode = 'Dpi'
$form.Font = New-Object Drawing.Font('Microsoft YaHei UI',10)
$form.BackColor = [Drawing.ColorTranslator]::FromHtml('#F5F7FA')
function LabelAt($parent,$x,$y,$w,$h,$size=10) {
    $c=New-Object Windows.Forms.Label; $c.SetBounds($x,$y,$w,$h); $c.Font=New-Object Drawing.Font('Microsoft YaHei UI',$size); $c.ForeColor=[Drawing.ColorTranslator]::FromHtml('#26364A'); $parent.Controls.Add($c); return $c
}
function ButtonAt($parent,$x,$y,$w,$h) {
    $c=New-Object Windows.Forms.Button; $c.SetBounds($x,$y,$w,$h); $c.FlatStyle='Flat'; $c.FlatAppearance.BorderColor=[Drawing.ColorTranslator]::FromHtml('#CAD3DE'); $c.BackColor=[Drawing.Color]::White; $parent.Controls.Add($c); return $c
}
function TextAt($parent,$x,$y,$w) {
    $c=New-Object Windows.Forms.TextBox; $c.SetBounds($x,$y,$w,30); $parent.Controls.Add($c); return $c
}
function ComboAt($parent,$x,$y,$w,$items) {
    $c=New-Object Windows.Forms.ComboBox; $c.SetBounds($x,$y,$w,32); $c.DropDownStyle='DropDownList'; $c.Items.AddRange([object[]]$items); $parent.Controls.Add($c); return $c
}
$header=New-Object Windows.Forms.Panel; $header.SetBounds(0,0,900,95); $header.BackColor=[Drawing.ColorTranslator]::FromHtml('#17283F'); $form.Controls.Add($header)
$brand=LabelAt $header 30 17 450 37 22; $brand.Text='D18 Setup'; $brand.ForeColor=[Drawing.Color]::White
$subtitle=LabelAt $header 32 58 590 24 10; $subtitle.ForeColor=[Drawing.Color]::LightSteelBlue
$languageLabel=LabelAt $header 676 34 72 25 9; $languageLabel.ForeColor=[Drawing.Color]::White
$languageBox=ComboAt $header 750 30 119 @('中文','English'); $languageBox.SelectedIndex= $(if($Language -eq 'zh'){0}else{1})
$crumb=LabelAt $form 32 111 835 28 10
$title=LabelAt $form 32 153 835 42 20
$hint=LabelAt $form 34 204 825 43 10
$pages=@()
for($i=0;$i -lt 4;$i++){ $p=New-Object Windows.Forms.Panel; $p.SetBounds(34,256,832,257); $form.Controls.Add($p); $pages+=,$p }
$exeLabel=LabelAt $pages[0] 0 0 650 26
$exeBox=TextAt $pages[0] 0 31 708; $exeBrowse=ButtonAt $pages[0] 724 29 107 33
$folderLabel=LabelAt $pages[0] 0 85 800 26
$folderValue=LabelAt $pages[0] 0 119 825 74 11
$findGames=ButtonAt $pages[0] 0 205 230 34
$runningGame=ButtonAt $pages[0] 246 205 270 34
$apiLabel=LabelAt $pages[1] 0 0 360 25
$apiBox=ComboAt $pages[1] 0 29 385 @('DirectX 11','DirectX 12','Vulkan')
$proxyLabel=LabelAt $pages[1] 425 0 400 25
$proxyBox=ComboAt $pages[1] 425 29 406 @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll'); $proxyBox.SelectedIndex=0
$proxyHint=LabelAt $pages[1] 0 76 830 43 10
$refLabel=LabelAt $pages[1] 0 134 420 26
$refBox=ComboAt $pages[1] 425 130 406 $script:words[$Language].refItems; $refBox.SelectedIndex=0
$refHint=LabelAt $pages[1] 0 181 830 65 10
$dependencyButton=ButtonAt $pages[1] 0 130 390 36
$runtimeLabel=LabelAt $pages[2] 0 0 760 26
$runtimeBox=TextAt $pages[2] 0 31 708; $runtimeBrowse=ButtonAt $pages[2] 724 29 107 33
$runtimeHint=LabelAt $pages[2] 0 87 820 76 11
$review=New-Object Windows.Forms.TextBox; $review.SetBounds(0,0,830,136); $review.Multiline=$true; $review.ReadOnly=$true; $review.ScrollBars='Vertical'; $review.BorderStyle='None'; $review.BackColor=$form.BackColor; $pages[3].Controls.Add($review)
$reviewNote=LabelAt $pages[3] 0 143 830 57 9
$ack=New-Object Windows.Forms.CheckBox; $ack.SetBounds(0,200,830,56); $pages[3].Controls.Add($ack); $ack.Visible=$false
$status=LabelAt $form 34 522 830 51 10
$progress=New-Object Windows.Forms.ProgressBar; $progress.SetBounds(34,578,830,4); $progress.Style='Marquee'; $progress.Visible=$false; $form.Controls.Add($progress)
$back=ButtonAt $form 586 604 116 36
$next=ButtonAt $form 718 604 148 36; $next.BackColor=[Drawing.ColorTranslator]::FromHtml('#245CC5'); $next.ForeColor=[Drawing.Color]::White
$uninstall=ButtonAt $form 34 604 156 36
$details=ButtonAt $form 206 604 140 36; $details.Enabled=$false
$logBox=New-Object Windows.Forms.TextBox; $logBox.SetBounds(34,664,832,195); $logBox.Multiline=$true; $logBox.ReadOnly=$true; $logBox.ScrollBars='Both'; $logBox.WordWrap=$false; $logBox.Font=New-Object Drawing.Font('Consolas',9); $form.Controls.Add($logBox)
$script:logVisible=$false
function Refresh-Page {
    $w=$script:words[$script:lang]
    $subtitle.Text=T subtitle; $languageLabel.Text=T language
    $crumb.Text=($w.steps | ForEach-Object { $_ }) -join '     /     '
    $title.Text=$w.titles[$script:step]; $hint.Text=$w.hints[$script:step]
    for($i=0;$i -lt 4;$i++){ $pages[$i].Visible=($i -eq $script:step); $pages[$i].Enabled=($null -eq $script:job -and -not $script:complete) }
    $exeLabel.Text=T exe; $folderLabel.Text=T folder; $exeBrowse.Text=T browse; $runtimeBrowse.Text=T browse
    $apiLabel.Text=T api; $proxyLabel.Text=T proxy; $proxyHint.Text=T proxyHint; $refLabel.Text=T ref; $refHint.Text=T refHint
    $ri=$refBox.SelectedIndex; $refBox.Items.Clear(); $refBox.Items.AddRange([object[]]$w.refItems); $refBox.SelectedIndex=[Math]::Max(0,$ri)
    $refLabel.Visible=$false; $refBox.Visible=$false
    $dependencyButton.Text=T dependencies; $findGames.Text=T games; $runningGame.Text=T running
    $runtimeLabel.Text=T runtime; $runtimeHint.Text=T runtimeHint; $back.Text=T back; $uninstall.Text=T uninstall
    $details.Text=$(if($script:logVisible){T hideDetails}else{T details})
    $back.Enabled=($script:step -gt 0 -and $null -eq $script:job -and -not $script:complete)
    $uninstall.Enabled=($null -eq $script:job -and -not $script:complete)
    $next.Enabled=($null -eq $script:job)
    $next.Text=if($script:complete){T finish}elseif($script:step -eq 3){T install}elseif($script:step -eq 2){T check}else{T next}
    $back.Visible=-not $script:complete; $uninstall.Visible=-not $script:complete
    $reviewNote.Visible=-not $script:complete
    $ack.Text=T ack
    if($script:step -eq 3){
        $review.Text=(T summary) -f [IO.Path]::GetFileName($exeBox.Text),$folderValue.Text,$apiBox.Text,$proxyBox.Text,$runtimeBox.Text
        $keepLabel=if($script:lang -eq 'zh'){'保留已有'}else{'Keep existing'}
        $localLabel=if($script:lang -eq 'zh'){'本地文件'}else{'Local file'}
        $srChoice=if($script:options.srMode -eq 'Download'){$script:options.srEntry.version}elseif($script:options.srMode -eq 'Local'){$localLabel}else{$keepLabel}
        $fgChoice=if($script:options.fgMode -eq 'Download'){$script:options.fgEntry.version}elseif($script:options.fgMode -eq 'Local'){$localLabel}else{$keepLabel}
        $review.Text+="`r`nSR: $srChoice   FG: $fgChoice"
        $notes=@((T reviewWarning)); if($script:check -and $script:check.data.proxy_exists){$notes+=(T proxyWarning)}
        if($script:check){
            if($script:check.data.re_pending){$notes+= $(if($script:lang -eq 'zh'){'REF 尚未准备：本次仅安装 D18，启动游戏前需补齐 REF。'}else{'REF is missing: this installs D18 only. Prepare REF before launching.'})}
            $missing=@($script:check.data.system.vc_missing)+@($script:check.data.system.graphics_missing)
            if($missing.Count){$notes+= $(if($script:lang -eq 'zh'){'缺少系统依赖：'}else{'Missing system dependencies: '})+($missing -join ', ')}
        }
        $reviewNote.Text=$notes -join "`r`n"
        $ack.Visible=($script:check -and @($script:check.data.anti_cheat).Count -gt 0)
    }
    if($script:lastCode){$status.Text=T $script:lastCode}else{$status.Text=T ready}
    $title.ForeColor=[Drawing.ColorTranslator]::FromHtml('#26364A')
    if($script:complete){
        $title.Text=if($script:lastCode -eq 'pending'){T pendingTitle}elseif($script:lastAction -eq 'Uninstall'){T removedTitle}else{T installedTitle}
        $hint.Text=if($script:lastCode -eq 'pending'){T pending}elseif($script:lastAction -eq 'Uninstall'){T removed}else{T installedHint}
        $title.ForeColor=if($script:lastCode -eq 'pending'){[Drawing.Color]::DarkGoldenrod}else{[Drawing.ColorTranslator]::FromHtml('#197347')}
        $crumb.Text=T successTitle
        $ack.Visible=$false
    }
}
function Show-Status([string]$code,[bool]$errorState=$false){
    $script:lastCode=$code; $status.Text=T $code
    $status.ForeColor=if($errorState){[Drawing.Color]::Firebrick}else{[Drawing.ColorTranslator]::FromHtml('#26364A')}
}
function Update-Game {
    $script:check=$null; $ack.Checked=$false
    try {
        if(Test-Path -LiteralPath $exeBox.Text -PathType Leaf){
            $resolved=[D18.Bootstrap]::Resolve($exeBox.Text)
            if($resolved -ine $exeBox.Text){$exeBox.Text=$resolved;return}
            $game=Split-Path -Parent ([IO.Path]::GetFullPath($exeBox.Text)); $folderValue.Text=$game
            if($script:selectionGame -ine $game){
                $script:selectionGame=$game
                $script:options=@{srMode='Keep';fgMode='Keep';srPath='';fgPath='';srEntry=$null;fgEntry=$null;ref='Auto';refPath='';refConfirm=$false;reEngine=$false;cache=$null}
                $runtimeBox.Text=''
            }
            $profile=Get-D18ReProfile -Game $game
            $proxyBox.SelectedItem=(Get-D18ProxyRecommendation -Game $game -IsRE $profile.IsRE).Name
            $apiBox.SelectedIndex=-1
            $state=Join-Path $game '.dlssnr-d18-install.json'
            if(Test-Path -LiteralPath $state){
                $old=Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
                $proxyBox.SelectedItem=[string]$old.proxy_name
                $apiBox.SelectedIndex=[Array]::IndexOf(@('DX11','None','Vulkan'),[string]$old.native_api)
            } elseif([IO.Path]::GetFileName($exeBox.Text) -ieq 'nioh2.exe'){$apiBox.SelectedIndex=0}
            elseif($profile.IsRE){$apiBox.SelectedIndex=1}
            foreach($name in @('D24Runtime.dll','nvngx_dlssnr.dll')){ $p=Join-Path $game $name; if(Test-Path -LiteralPath $p){$runtimeBox.Text=$p;break} }
        } else { $folderValue.Text='' }
    } catch { $folderValue.Text=''; Show-Status game_exe $true }
}
function Begin-Operation([string]$action){
    try {
        $request=Get-D18GuiRequest -Action $action -Exe $exeBox.Text -ApiIndex $apiBox.SelectedIndex -Proxy $proxyBox.Text -Runtime $runtimeBox.Text -Ref @('Auto','Recommended','Latest','Existing','Manual')[$refBox.SelectedIndex] -Ack $ack.Checked
        foreach($key in $script:options.Keys){$request[$key]=$script:options[$key]}
        if($script:check){$request.preparedPlan=$script:check.data.prepared_plan;$request.preparedRef=$script:check.data.prepared_ref}
        $script:job=Start-D18GuiOperation -Request $request -SessionRoot $script:sessionRoot
        $script:lastAction=$action; $progress.Visible=$true; $details.Enabled=$true
        Show-Status $(if($action -eq 'Check'){'checking'}elseif($action -eq 'Install'){'installing'}else{'removing'})
        Refresh-Page
    } catch {
        $code=if($_.Exception.Message -match 'GAME_EXE'){'game_exe'}elseif($_.Exception.Message -match 'API|PROXY'){'api_error'}elseif($_.Exception.Message -match 'RUNTIME_PATH'){'runtime_path'}else{'operation_failed'}
        Show-Status $code $true; $logBox.Text=$_.Exception.ToString(); $details.Enabled=$true
    }
}
function Select-GameCandidate($Candidates) {
    $pick=New-Object Windows.Forms.Form; $pick.Text=T exe; $pick.ClientSize=New-Object Drawing.Size(860,450);$pick.StartPosition='CenterParent'
    $search=TextAt $pick 16 15 825
    $list=New-Object Windows.Forms.ListBox;$list.SetBounds(16,57,825,326);$list.HorizontalScrollbar=$true;$list.DisplayMember='display';$pick.Controls.Add($list)
    $all=@($Candidates|ForEach-Object {[pscustomobject]@{display=([string]$_.name+'  —  '+[string]$_.path);path=$_.path}})
    foreach($item in $all){$null=$list.Items.Add($item)}
    $choose=ButtonAt $pick 686 398 155 34;$choose.Text=T next
    $selection=@{path=$null}
    $search.Add_TextChanged({$list.Items.Clear();foreach($item in $all){if($item.display.IndexOf($search.Text,[StringComparison]::OrdinalIgnoreCase) -ge 0){$null=$list.Items.Add($item)}}})
    $choose.Add_Click({if($list.SelectedItem){$selection.path=$list.SelectedItem.path;$pick.Close()}})
    $list.Add_DoubleClick({$choose.PerformClick()});$null=$pick.ShowDialog($form);$pick.Dispose()
    if($selection.path){$exeBox.Text=$selection.path}
}
$findGames.Add_Click({
    try{$script:job=Start-D18GuiOperation -Request @{action='Discover'} -SessionRoot $script:sessionRoot;Show-Status discovering;$progress.Visible=$true;Refresh-Page}
    catch{Show-Status operation_failed $true}
})
$runningGame.Add_Click({$c=@(Get-Process|Where-Object {$_.MainWindowHandle -ne 0 -and $_.Path}|ForEach-Object {[pscustomobject]@{name=$_.ProcessName;path=$_.Path}});Select-GameCandidate $c})
$dependencyButton.Add_Click({$null=Show-D18Dependencies -Owner $form -Options $script:options -Game $folderValue.Text -Lang $script:lang -SessionRoot $script:sessionRoot})
$exeBrowse.Add_Click({$d=New-Object Windows.Forms.OpenFileDialog; $d.Filter='Game executable (*.exe)|*.exe'; if($d.ShowDialog($form) -eq 'OK'){$exeBox.Text=$d.FileName};$d.Dispose()})
$runtimeBrowse.Add_Click({$d=New-Object Windows.Forms.OpenFileDialog; $d.Filter='DLSS-NR (*.dll)|*.dll'; if($d.ShowDialog($form) -eq 'OK'){$runtimeBox.Text=$d.FileName};$d.Dispose()})
$exeBox.Add_TextChanged({Update-Game})
$languageBox.Add_SelectedIndexChanged({$script:lang=if($languageBox.SelectedIndex -eq 0){'zh'}else{'en'}; Refresh-Page})
$back.Add_Click({if($script:step -gt 0){$script:step--; $script:check=$null; $ack.Checked=$false; $script:lastCode=''; Refresh-Page}})
$next.Add_Click({
    if($script:complete){$form.Close();return}
    if($script:step -eq 0){
        if(-not(Test-Path -LiteralPath $exeBox.Text -PathType Leaf) -or [IO.Path]::GetExtension($exeBox.Text) -ine '.exe'){Show-Status game_exe $true;return}
        $script:step=1
    } elseif($script:step -eq 1){
        if($apiBox.SelectedIndex -lt 0){Show-Status api_error $true;return}
        if(-not(Show-D18Dependencies -Owner $form -Options $script:options -Game $folderValue.Text -Lang $script:lang -SessionRoot $script:sessionRoot)){return}
        $script:step=2
    } elseif($script:step -eq 2){Begin-Operation Check;return}
    else {
        if($ack.Visible -and -not $ack.Checked){Show-Status ackNeeded $true;return}
        Begin-Operation Install;return
    }
    $script:lastCode=''; Refresh-Page
})
$uninstall.Add_Click({
    if(-not(Test-Path -LiteralPath $exeBox.Text -PathType Leaf)){Show-Status game_exe $true;return}
    if([Windows.Forms.MessageBox]::Show($form,((T uninstallConfirm) -f $folderValue.Text),(T uninstall),'YesNo','Question') -eq 'Yes'){Begin-Operation Uninstall}
})
$details.Add_Click({$script:logVisible=-not $script:logVisible; $form.ClientSize=New-Object Drawing.Size(900,$(if($script:logVisible){883}else{660})); Refresh-Page})
$timer=New-Object Windows.Forms.Timer; $timer.Interval=200
$timer.Add_Tick({
    if($script:job -and $script:job.process.HasExited){
        $finished=$script:job; $script:job=$null; $progress.Visible=$false
        try {
            $r=Get-Content -LiteralPath $finished.result -Raw -Encoding UTF8 | ConvertFrom-Json
            $logBox.Text=(T logPath)+$finished.directory+"`r`n`r`n"+$r.message+"`r`n"+($r.data | ConvertTo-Json -Depth 12)
            if($r.success){
                if($r.action -eq 'Discover'){Select-GameCandidate @($r.data);Show-Status ready}
                elseif($r.action -eq 'Check'){$script:check=$r; $script:step=3; Show-Status checked}
                else {$script:complete=$true; Show-Status $(if($r.action -eq 'Install' -and $r.data.re_pending){'pending'}elseif($r.action -eq 'Install'){'done'}else{'removed'})}
            } else {Show-Status ([string]$r.code) $true}
        } catch {
            $logBox.Text=(T logPath)+$finished.directory+"`r`n"+$_.Exception.Message
            Show-Status operation_failed $true
        }
        $finished.process.Dispose(); Refresh-Page
    }
})
$form.Add_FormClosing({param($sender,$e) if($script:job){$e.Cancel=$true; Show-Status busyClose $true}})
Refresh-Page
if($UiSmokeDirectory){
    $null=New-Item -ItemType Directory -Path $UiSmokeDirectory -Force
    $script:sessionRoot=Join-Path $UiSmokeDirectory 'jobs'
    $game=Join-Path $UiSmokeDirectory 'game';$null=New-Item -ItemType Directory -Path $game -Force
    $exe=Join-Path $game 'ui-fixture.exe';[IO.File]::WriteAllText($exe,'UI integration fixture')
    $form.Show();$timer.Start();$exeBox.Text=$exe;$runtimeBox.Text=$SmokeRuntime
    $next.PerformClick()
    if($script:step -ne 1){throw 'Game selection did not advance.'}
    $apiBox.SelectedIndex=0;$languageBox.SelectedIndex=0;$languageBox.SelectedIndex=1
    if($exeBox.Text -ne $exe -or $runtimeBox.Text -ne $SmokeRuntime){throw 'Language switching lost selections.'}
    $testOptions=$script:options.Clone();$testOptions.cache=Join-Path (Split-Path -Parent $PSScriptRoot) 'download-test'
    $null=Show-D18Dependencies -Owner $form -Options $testOptions -Game $game -Lang 'zh' -SessionRoot $script:sessionRoot -SmokeResult (Join-Path $UiSmokeDirectory 'dependency-results.json')
    $script:step=2;Refresh-Page;$next.PerformClick()
    $deadline=[DateTime]::UtcNow.AddMinutes(3)
    while($script:job -and [DateTime]::UtcNow -lt $deadline){[Windows.Forms.Application]::DoEvents();[Threading.Thread]::Sleep(20)}
    if($script:step -ne 3){throw ('UI preflight failed: '+$logBox.Text)}
    $next.PerformClick()
    while($script:job -and [DateTime]::UtcNow -lt $deadline){[Windows.Forms.Application]::DoEvents();[Threading.Thread]::Sleep(20)}
    if(-not $script:complete -or -not(Test-Path -LiteralPath (Join-Path $game '.dlssnr-d18-install.json'))){throw ('UI install failed: '+$logBox.Text)}
    $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
    $form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$form.Width,$form.Height)));$bitmap.Save((Join-Path $UiSmokeDirectory 'installed.png'));$bitmap.Dispose()
    $script:complete=$false;Begin-Operation Uninstall
    while($script:job -and [DateTime]::UtcNow -lt $deadline){[Windows.Forms.Application]::DoEvents();[Threading.Thread]::Sleep(20)}
    if(Test-Path -LiteralPath (Join-Path $game '.dlssnr-d18-install.json')){throw 'UI uninstall failed.'}
    @{selection=$true;language_preserved=$true;async_check=$true;install=$true;uninstall=$true}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $UiSmokeDirectory 'results.json') -Encoding UTF8
    $form.Close();$timer.Dispose();$form.Dispose();return
}
if($PreviewDirectory){
    # Render the actual form without installing or opening any game.
    $null=New-Item -ItemType Directory -Path $PreviewDirectory -Force
    $exeBox.Text='D:\Games\Example Game\Game.exe'; $folderValue.Text='D:\Games\Example Game'
    $runtimeBox.Text='D:\My files\nvngx_dlssnr.dll'; $apiBox.SelectedIndex=0
    $form.Show()
    foreach($l in @('zh','en')){
        $script:lang=$l; $languageBox.SelectedIndex=$(if($l -eq 'zh'){0}else{1})
        for($s=0;$s -lt 4;$s++){
            $script:step=$s; $script:lastCode='preview'; Refresh-Page; [Windows.Forms.Application]::DoEvents()
            $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
            $form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$form.Width,$form.Height)))
            $bitmap.Save((Join-Path $PreviewDirectory "$l-$s.png")); $bitmap.Dispose()
        }
        foreach($state in @('done','pending','removed')){
            $script:complete=$true;$script:lastCode=$state;$script:lastAction=if($state -eq 'removed'){'Uninstall'}else{'Install'}
            Refresh-Page;[Windows.Forms.Application]::DoEvents()
            $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
            $form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$form.Width,$form.Height)))
            $bitmap.Save((Join-Path $PreviewDirectory "$l-$state.png"));$bitmap.Dispose()
        }
        $script:complete=$false;$script:lastCode='preview';Refresh-Page
        $null=Show-D18Dependencies -Owner $form -Options $script:options -Game $PSScriptRoot -Lang $l -SessionRoot $PreviewDirectory -PreviewPath (Join-Path $PreviewDirectory ($l+'-dependencies.png'))
    }
    $form.Close()
} else { $timer.Start(); [Windows.Forms.Application]::Run($form) }
$timer.Dispose(); $form.Dispose()
