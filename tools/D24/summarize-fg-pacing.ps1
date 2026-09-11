param([Parameter(Mandatory=$true)][string]$LogPath,[string]$OutputPath)
$ErrorActionPreference='Stop'
$warmupCount=0; $warmupMs=0L; $warmupMax=0; $warmupMin=[int]::MaxValue
$toggles=0; $disabled=0; $dispatch=0; $intervals=@{}
$clampWarnings=0; $interpolationTransitions=@()
$pacing=@{}
foreach($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $LogPath).Path)) {
 if($line -match 'D18 pacing stage=([\w-]+) (.*)') {
  $stage=$matches[1]; $fields=$matches[2]
  foreach($m in [regex]::Matches($fields,'(\w+Ms)=([\d.]+)')) {
   $key=$stage+'.'+$m.Groups[1].Value
   $value=[double]::Parse($m.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture)
   if(!$pacing.ContainsKey($key)){$pacing[$key]=@{Count=0;SumMs=0.0;MaxMs=0.0}}
   $pacing[$key].Count++; $pacing[$key].SumMs+=$value
   $pacing[$key].MaxMs=[Math]::Max($pacing[$key].MaxMs,$value)
  }
 }
 if($line -match 'Blocked for warmup for (\d+) ms') {
  $ms=[int]$matches[1]; $warmupCount++; $warmupMs+=$ms
  $warmupMax=[Math]::Max($warmupMax,$ms); $warmupMin=[Math]::Min($warmupMin,$ms)
 }
 if($line.Contains('RSYNC: Status toggled')){$toggles++}
 if($line.Contains('DLSSG_Dx12::EvaluateState !FGEnabled')){$disabled++}
 if($line.Contains('DLSSG_Dx12::Dispatch')){$dispatch++}
 if($line.Contains('VSync interval 2 not supported with FG')){$clampWarnings++}
 if($line.Contains('DLSS-G interpolation state changed') -and $interpolationTransitions.Count -lt 32){$interpolationTransitions+=$line}
 if($line -match 'hkFGPresent SyncInterval: (\d+)'){$key=$matches[1];$intervals[$key]++}
}
$result=[ordered]@{
 Log=(Resolve-Path -LiteralPath $LogPath).Path
 WarmupCount=$warmupCount; WarmupTotalMs=$warmupMs
 WarmupMinMs=$(if($warmupCount){$warmupMin}else{$null});WarmupMaxMs=$(if($warmupCount){$warmupMax}else{$null})
 RsyncStateToggles=$toggles; FGDisabledObservations=$disabled; DispatchLogLines=$dispatch
 PresentSyncIntervals=$intervals
 Interval2ClampWarningLines=$clampWarnings; InterpolationTransitionsFirst32=$interpolationTransitions
 SampledCpuPacing=$pacing
 Limitation='Log observations only. Durations are reported RSYNC waits, not exclusive GPU timings. Missing dispatch logs alone do not prove absence without adequate log coverage.'
} | ConvertTo-Json -Depth 4
if($OutputPath){[IO.File]::WriteAllText($OutputPath,$result)}
$result
