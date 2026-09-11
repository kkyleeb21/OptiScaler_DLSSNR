# Process-level baseline companion to summarize_runtime.py. No game input or
# configuration changes; counters describe the whole process, not D18 alone.
[CmdletBinding()]
param(
 [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$TargetProcessId,
 [Parameter(Mandatory)][string]$OutputPath,
 [ValidateRange(1,7200)][int]$DurationSeconds=600,
 [ValidateRange(1,30)][int]$IntervalSeconds=2,
 [string[]]$ObservedFiles=@(),
 [switch]$CollectGpuMemory
)
$ErrorActionPreference='Stop'
$targetProcess=Get-Process -Id $TargetProcessId
$startIdentity=$targetProcess.StartTime.ToUniversalTime()
$startCpu=$targetProcess.TotalProcessorTime.TotalSeconds
$clock=[Diagnostics.Stopwatch]::StartNew()
$writer=[IO.StreamWriter]::new([IO.File]::Open($OutputPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read))
try {
 while($clock.Elapsed.TotalSeconds -lt $DurationSeconds -and $writer.BaseStream.Position -lt 16MB){
  $targetProcess=Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
  if(!$targetProcess -or $targetProcess.StartTime.ToUniversalTime() -ne $startIdentity){break}
  $io=$null
  try {$io=Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$TargetProcessId" -ErrorAction Stop | Select-Object -First 1} catch {}
  $files=@(foreach($observedFile in $ObservedFiles){
   $item=Get-Item -LiteralPath $observedFile -ErrorAction SilentlyContinue
   @{path=$observedFile;bytes=if($item){$item.Length}else{$null}}
  })
  $gpuDedicated=$null;$gpuShared=$null;$gpuCoverage='not_collected'
  if($CollectGpuMemory){
   try {
    $counter=(Get-Counter -Counter '\GPU Process Memory(*)\Dedicated Usage','\GPU Process Memory(*)\Shared Usage' -ErrorAction Stop).CounterSamples
    $matching=@($counter|Where-Object {$_.InstanceName -like "pid_${TargetProcessId}_*"})
    $dedicated=@($matching|Where-Object {$_.Path -like '*\Dedicated Usage'})
    $shared=@($matching|Where-Object {$_.Path -like '*\Shared Usage'})
    if($dedicated.Count -gt 0 -and $shared.Count -gt 0 -and @($matching|Where-Object {$_.Status -notin @(0,1) -or $_.CookedValue -lt 0}).Count -eq 0){
     $gpuDedicated=($dedicated|Measure-Object CookedValue -Sum).Sum
     $gpuShared=($shared|Measure-Object CookedValue -Sum).Sum
     $gpuCoverage='process_pdh_all_adapters'
    }else{$gpuCoverage='process_instance_missing_or_invalid'}
   }catch{$gpuCoverage='counter_unavailable'}
  }
  $row=@{schema=1;event='process_sample';utc=[DateTime]::UtcNow.ToString('o');pid=$TargetProcessId;
   elapsed_seconds=$clock.Elapsed.TotalSeconds;cpu_seconds_since_start=$targetProcess.TotalProcessorTime.TotalSeconds-$startCpu;
   private_bytes=$targetProcess.PrivateMemorySize64;working_set_bytes=$targetProcess.WorkingSet64;
   handles=$targetProcess.HandleCount;threads=$targetProcess.Threads.Count;
   io_read_bytes_per_second=if($io){$io.IOReadBytesPersec}else{$null};
   io_write_bytes_per_second=if($io){$io.IOWriteBytesPersec}else{$null};files=$files;
   gpu_dedicated_bytes=$gpuDedicated;gpu_shared_bytes=$gpuShared;
   gpu_coverage=$gpuCoverage;frame_time_coverage='not_collected';scope='whole_process'}
  $writer.WriteLine(($row|ConvertTo-Json -Depth 4 -Compress));$writer.Flush()
  Start-Sleep -Seconds $IntervalSeconds
 }
} finally {$writer.Dispose()}
