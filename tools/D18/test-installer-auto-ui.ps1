#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$Installer,[Parameter(Mandatory=$true)][string]$Game,
      [Parameter(Mandatory=$true)][string]$Runtime,[Parameter(Mandatory=$true)][string]$Output,[string]$Cache)
$ErrorActionPreference='Stop'
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules;C:\Program Files\WindowsPowerShell\Modules"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
. (Join-Path $Installer 'D18-Common.ps1')
. (Join-Path $Installer 'D18-Dependencies.ps1')
. (Join-Path $Installer 'D18-REFramework.ps1')
. (Join-Path $Installer 'D18-GuiCommon.ps1')
. (Join-Path $Installer 'D18-DependencyDialog.ps1')
# Reuse the actual control constructors without executing the main installer window.
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $Installer 'D18-Setup.ps1'),[ref]$tokens,[ref]$errors)
foreach($f in $ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -in @('LabelAt','ButtonAt','TextAt','ComboAt')},$false)){. ([scriptblock]::Create($f.Extent.Text))}
$null=New-Item -ItemType Directory -Path $Output -Force
$options=@{srMode='Keep';fgMode='Keep';srPath='';fgPath='';srEntry=$null;fgEntry=$null;ref='Auto';refPath='';refConfirm=$false;reEngine=$false;cache=$Cache}
$owner=New-Object Windows.Forms.Form
try {
 $null=Show-D18Dependencies -Owner $owner -Options $options -Game $Game -Lang 'zh' -SessionRoot $Output -Api None -Runtime $Runtime -AutoSmokeResult (Join-Path $Output 'result.json')
 Get-Content -LiteralPath (Join-Path $Output 'result.json') -Raw
} finally {$owner.Dispose()}
