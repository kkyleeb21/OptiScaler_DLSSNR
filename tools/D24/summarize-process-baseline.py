import json
from pathlib import Path

import argparse
parser=argparse.ArgumentParser(description="Summarize bounded process samples and matched native log snapshots; no gameplay verdict.")
parser.add_argument("directory",type=Path)
parser.add_argument("--phase",choices=["nr-on","nr-off","exposure-on","exposure-off"],default="nr-on")
args=parser.parse_args()
root=args.directory
phase=args.phase
def read(path):
    rows=[]
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        try: rows.append(json.loads(line))
        except ValueError: pass  # live-copy tail can end mid-record
    return rows
samples=read(root/f'{phase}-process.jsonl')
start=read(root/f'{phase}-start.log');end=read(root/f'{phase}-end.log')
start_windows=sum(r.get('event')=='dx11_performance' for r in start)
windows=[r for r in end if r.get('event')=='dx11_performance'][start_windows:]
a,b=samples[0],samples[-1];seconds=b['elapsed_seconds']-a['elapsed_seconds']
frames=sum(w['frames'] for w in windows)
result={'pid':a['pid'],'utc_start':a['utc'],'utc_end':b['utc'],'sample_count':len(samples),'seconds':seconds,
        'cpu_average_logical_cores':(b['cpu_seconds_since_start']-a['cpu_seconds_since_start'])/seconds,
        'process_memory':{key:{'start':a[key],'end':b[key],'min':min(s[key] for s in samples),'max':max(s[key] for s in samples),'delta':b[key]-a[key]}
                          for key in ['private_bytes','working_set_bytes','handles','threads']},
        'file_growth':[{ 'path':x['path'],'start_bytes':x['bytes'],'end_bytes':y['bytes'],'bytes_per_second':(y['bytes']-x['bytes'])/seconds}
                       for x,y in zip(a['files'],b['files'])],
        'native_windows':len(windows),'native_calls_in_windows':frames,
        'window_mode_values':sorted(set(w['mode'] for w in windows)),
        'last_result_values':sorted(set(w['last_result'] for w in windows)),
        'native_average_per_call':{key:(sum(w[key] for w in windows)/frames if frames else None) for key in ['total_us','model_wait_us','compose_wait_us','polls','srv_attempts','uav_attempts']},
        'exposure_allocation_attempts_session':sorted(set(w['exposure_allocation_attempts_session'] for w in windows)),
        'resident_resources':sorted(set(w['resident_resources'] for w in windows)),
        'inflight_resources_at_window_end':sorted(set(w['inflight_resources'] for w in windows)),
        'limitations':['Short single-phase baseline, not leak proof or a before/after speedup measurement.',
                        'Window boundaries can straddle capture start/end; first partial window is not isolated.',
                        'CPU wall time includes GPU waiting; not GPU duration or whole-game frame time.',
                        'Whole-process memory/CPU and file growth cannot all be attributed to D18.',
                        'Per-process GPU memory and IO counters unavailable; no zero substituted.']}
(root/f'{phase}-summary.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,indent=2))
