#Requires -Version 5.1
[CmdletBinding()]
param([string]$ScratchRoot = [System.IO.Path]::GetTempPath())
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\D18-Common.ps1')
# Tiny synthetic fixtures exercise classification without redistributing NVIDIA bytes.
$scratch = Join-Path $ScratchRoot ('D18-runtime-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
function Fixture($Head,[bool]$Patched=$false,[bool]$HookOnly=$false) {
 $a=New-Object byte[] $(if($Patched){1024}else{512})
 [Array]::Copy([byte[]]$Head,0,$a,0,$Head.Length)
 [Array]::Copy([BitConverter]::GetBytes([uint32]128),0,$a,60,4)
 [Array]::Copy([BitConverter]::GetBytes([uint16]1),0,$a,134,2)
 [Array]::Copy([BitConverter]::GetBytes([uint32]8192),0,$a,208,4)
 foreach($x in @(@(400,512),@(404,4096),@(408,512),@(412,512),@(428,1610612736))) {
  [Array]::Copy([BitConverter]::GetBytes([uint32]$x[1]),0,$a,[int]$x[0],4)
 }
 if($Patched){$a[512]=9;$a[513]=10}
 return ,$a
}
$original=Fixture ([byte[]](1,2,3,4,5,6,7,8))
$expected=Fixture ([byte[]](1,20,30,4,5,6,7,8)) $true
$source = Join-Path $scratch 'input.dll'
$output = Join-Path $scratch 'output.dll'
$manifestPath = Join-Path $scratch 'patch.json'
[IO.File]::WriteAllBytes($source, $original)
[IO.File]::WriteAllBytes($output, $expected)
$manifest = @{
    format = 'dlssnr-d18-guarded-layout-patch-v2'
    reference_source_sha256 = Get-D18Sha256 $source
    reference_output_sha256 = Get-D18Sha256 $output
    hunks = @(
        @{ offset = 1; expected_base64 = 'AgM='; replacement_base64 = 'FB4='; compatible_input_base64 = @('CxY=') },
        @{ offset = 512; expected_base64 = ''; replacement_base64 = [Convert]::ToBase64String([byte[]]$expected[512..1023]) }
    )
}
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6))
$cases = @(
    @{ name='reference'; bytes=$original; status='VERIFIED'; applied=2 },
    @{ name='reference-patched'; bytes=$expected; status='VERIFIED'; applied=0 },
    @{ name='unknown-unrelated-change'; bytes=(Fixture ([byte[]](99,2,3,4,5,6,7,8))); status='UNVERIFIED_COMPATIBLE'; applied=2 },
    @{ name='unknown-already-patched'; bytes=(Fixture ([byte[]](99,20,30,4,5,6,7,8)) $true); status='ALREADY_PATCHED'; applied=0 },
    @{ name='partial-patch'; bytes=(Fixture ([byte[]](99,20,30,4,5,6,7,8))); status='CONFLICT'; applied=0 },
    @{ name='known-field-variant'; bytes=(Fixture ([byte[]](99,11,22,4,5,6,7,8))); status='UNVERIFIED_COMPATIBLE'; applied=2 },
    @{ name='conflict'; bytes=(Fixture ([byte[]](1,255,3,4,5,6,7,8))); status='CONFLICT'; applied=0 },
    @{ name='unknown-append'; bytes=([byte[]](@($original)+@(255))); status='CONFLICT'; applied=0 },
    @{ name='truncated'; bytes=[byte[]](1,2); status='CONFLICT'; applied=0 }
)
foreach ($case in $cases) {
    [IO.File]::WriteAllBytes($source, $case.bytes)
    $before = Get-D18Sha256 $source
    # An existing output must also remain unchanged when preflight fails.
    [IO.File]::WriteAllBytes($output, [byte[]](77,88))
    $previousOutput = Get-D18Sha256 $output
    $result = $null
    $caught = $null
    try { $result = New-D18PatchedRuntime $source $output $manifestPath }
    catch { $caught = $_ }
    if ($case.status -eq 'CONFLICT') {
        if (-not $caught -or $caught.Exception.Message -notmatch '\[CONFLICT\]' -or
            (Get-D18Sha256 $output) -ne $previousOutput) { throw "Failed: $($case.name)" }
    } else {
        if ($caught) { throw $caught }
        if ($result.Classification -ne $case.status -or $result.AppliedHunks -ne $case.applied) {
            throw "Wrong classification/count: $($case.name)"
        }
        if ([IO.File]::ReadAllBytes($output)[0] -ne $case.bytes[0]) { throw 'Unrelated bytes changed' }
    }
    if ((Get-D18Sha256 $source) -ne $before) { throw 'Source modified' }
    Write-Output "PASS $($case.name)"
}
Write-Output "9 cases passed. Synthetic fixtures retained: $scratch"
