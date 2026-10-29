#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$SetupPath,
      [Parameter(Mandatory=$true)][string]$ResultsRoot)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName PresentationFramework
$assembly=[Reflection.Assembly]::LoadFrom($SetupPath)
$type=$assembly.GetType('D18.SetupWindow',$true)
# Test the compiled presentation method without opening UI or constructing Context,
# which would extract a bundle. No Runtime DLL is loaded.
$window=[Runtime.Serialization.FormatterServices]::GetUninitializedObject($type)
$flags=[Reflection.BindingFlags]'Instance,NonPublic'
$type.GetField('nrClass',$flags).SetValue($window,'CONFLICT')
$method=$type.GetMethod('NrMessage',$flags)
$count=0
foreach($path in Get-ChildItem -LiteralPath $ResultsRoot -Filter '*-bridge.json') {
    $result=[D18.Json]::Read($path.FullName)
    $data=[D18.Json]::D($result,'data')
    $type.GetField('nrData',$flags).SetValue($window,$data)
    foreach($language in @('zh','en')) {
        $type.GetField('language',$flags).SetValue($window,$language)
        $expected=[D18.Json]::S($data,$(if($language -eq 'zh'){'MessageZh'}else{'MessageEn'}))
        $actual=$method.Invoke($window,@())
        if(-not $expected -or $actual -cne $expected){throw "Compiled WPF message mismatch: $($path.Name) / $language"}
        $count++
    }
}
if($count -ne 16){throw "Expected 16 compiled WPF messages; got $count"}
Write-Output "PASS $count compiled WPF conflict messages, Chinese and English. No window opened."
