"""Read bounded Vulkan NR observations; recording is distinct from GPU completion."""
import argparse,hashlib,json,re
from collections import Counter
from pathlib import Path

def summarize(path):
    data=path.read_bytes()
    if len(data)>64*1024*1024:raise ValueError('Vulkan diagnostic log exceeds 64 MiB analysis budget')
    rows=[];invalid=0
    for line in data.decode('utf-8-sig',errors='replace').splitlines():
        row=dict(re.findall(r'([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)',line))
        if 'event' not in row:invalid+=1;continue
        for key,value in row.copy().items():
            if re.fullmatch(r'-?\d+',value):row[key]=int(value)
        rows.append(row)
    advanced=[r for r in rows if r['event']=='native_advanced']
    return dict(schema='d18-vulkan-nr-summary-v1',sha256=hashlib.sha256(data).hexdigest(),records=len(rows),invalid_lines=invalid,
        events=dict(Counter(r['event'] for r in rows)),advanced_recordings=advanced,
        failed_advanced_recordings=[r for r in advanced if r.get('result')!=1],
        submission_observations=[r for r in rows if r['event'] in {'nr_submit','completion_fence','gpu_completed','retirement_ready'}],
        coverage_observations=[r for r in rows if r['event'] in {'recording','last_barrier','storage_formats'}],
        boundary='Bounded observations, not session totals. NR recording success is not GPU completion. Completion events are separate and cannot be assigned to every pass without a matching epoch. Missing logs or absent validation-layer messages do not establish execution, compatibility or image quality.')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    a.output.write_text(json.dumps(summarize(a.log),indent=2),encoding='utf-8')
