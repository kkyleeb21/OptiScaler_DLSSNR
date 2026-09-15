"""Native DX11 FG input and runtime-present evidence; bounded observations only."""
import argparse,hashlib,json,re
from pathlib import Path

def summarize(path, journal=None):
    inputs=[];presents=[];errors=[];native_presents=[]
    raw=path.read_bytes() if path.is_file() else b''
    for n,line in enumerate(raw.decode('utf-8-sig',errors='replace').splitlines(),1):
        if 'd18_native_fg ' not in line and 'd18_fg_present ' not in line and 'd18_native_fg_present ' not in line:continue
        row=dict(re.findall(r'(\w+)=([^\s;]+)',line));row['line']=n
        for k,v in list(row.items()):
            if isinstance(v,str) and re.fullmatch(r'-?\d+',v):row[k]=int(v)
        if 'd18_native_fg stopped' in line:errors.append(row)
        elif 'd18_native_fg_present ' in line:native_presents.append(row)
        elif 'd18_native_fg ' in line:inputs.append(row)
        else:presents.append(row)
    good=[r for r in presents if r.get('query')==0 and r.get('status')==0 and r.get('present_hr')==0]
    result = dict(schema='d18-native-fg-summary-v1',sha256=hashlib.sha256(raw).hexdigest() if path.is_file() else None,
        input_samples=inputs,present_samples=presents,native_present_samples=native_presents,errors=errors,
        max_submitted=max((r.get('submitted',0) for r in inputs),default=0),
        valid_present_samples=len(good),generated_frame_samples=sum(r.get('last_generated_tick',0)>0 for r in native_presents),
        boundary='Inputs accepted is not generated-frame proof. SL reports frames since the previous GetState call. V35 native inputs query every present and aggregate separately in native_present_samples; legacy app_presents is a log cadence, not necessarily the query interval. Other clients may consume counters. Missing observations do not prove failure. Synthetic host success is not gameplay or visual acceptance.')
    if journal and journal.is_file():
        events=[]; malformed=0
        for number,line in enumerate(journal.read_text(encoding='utf-8-sig',errors='replace').splitlines(),1):
            if not line.strip():continue
            try:
                row=json.loads(line)
                if not isinstance(row,dict):raise ValueError('object expected')
                if isinstance(row.get('present_hr'),int):
                    value=row['present_hr'] & 0xffffffff
                    row['present_hr_hex']=f'0x{value:08X}'
                    row['present_hr_name']={0:'S_OK',0x087A0001:'DXGI_STATUS_OCCLUDED',
                        0x087A0007:'DXGI_STATUS_MODE_CHANGED',0x887A000A:'DXGI_ERROR_WAS_STILL_DRAWING',
                        0x887A0005:'DXGI_ERROR_DEVICE_REMOVED'}.get(value,'unmapped')
                events.append({'line':number,**row})
            except ValueError:malformed+=1
        counts={}
        for row in events:
            stage=row.get('stage','unknown');counts[stage]=counts.get(stage,0)+1
        result['sync_journal']={'sha256':hashlib.sha256(journal.read_bytes()).hexdigest(),
            'malformed_lines':malformed,'stage_counts':counts,'last_observations':events[-8:],
            'failures':[row for row in events if row.get('stage')=='stopped'],
            'recovery_events':[row for row in events if row.get('stage') in ('present_retry','present_resumed','recovery_complete')],
            'threads':sorted({row['thread'] for row in events if isinstance(row.get('thread'),int)}),
            'max_submitted':max((row.get('submitted',0) for row in events),default=0),
            'max_skipped':max((row.get('skipped',0) for row in events),default=0),
            'boundary':'Bounded independent journal. Recorded tags, queue submission, completion fence and observed completion are distinct. Pending input skips preserve SR/NR but are not generated-frame evidence. Exhausted budgets do not prove later success or failure.'}
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--journal',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    a.output.write_text(json.dumps(summarize(a.log,a.journal),indent=2),encoding='utf-8')
