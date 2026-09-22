"""Bounded, read-only adapter for V8 native events and archived capture metadata."""
from pathlib import Path
import argparse,json,re

def summarize(paths):
    events=[];captures=[];coverage=[]
    for name in paths:
        p=Path(name)
        if p.suffix.lower()=='.json':
            value=json.loads(p.read_text(encoding='utf-8-sig'))
            if isinstance(value,dict) and 'reconstruction_applied' in value:
                captures.append({'path':str(p),**{k:v for k,v in value.items() if k.startswith('reconstruction_') or k in ('v8_shader_sha256','pid','tick','request_id')}})
            coverage.append(dict(path=str(p),kind='capture_metadata',observed=bool(isinstance(value,dict) and 'reconstruction_applied' in value)))
        else:
            with p.open('rb') as f:
                size=f.seek(0,2); start=max(0,size-8*1024*1024);f.seek(start);text=f.read().decode('utf-8',errors='replace')
            rows=[]
            for line in text.splitlines():
                if 'event=v8_state ' in line:
                    rows.append(dict(re.findall(r'([A-Za-z_][\w]*)=([^\s]+)',line)))
            events.extend(rows)
            coverage.append(dict(path=str(p),kind='native_log',tail_only=start>0,events=len(rows)))
    applied=[x for x in events if x.get('applied')=='2']
    return dict(schema=1,coverage=coverage,events=events,captures=captures,
                native_V8_dispatch_observed=bool(applied),event_budget_may_be_exhausted=len(events)>=64,
                GPU_completion='not established by v8_state; use capture retirement/completion evidence separately',
                gameplay='requires user observation',hook_coverage={'Vulkan':'instrumented in Vulkan preview and later; actual observations above','DX12':'instrumented from D18_V8_DX12_PREVIEW_20260923; actual observations above','DX11':'not implemented in this preview'})

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('paths',nargs='+');p.add_argument('--out',type=Path);a=p.parse_args()
    result=json.dumps(summarize(a.paths),ensure_ascii=False,indent=2)
    if a.out:a.out.write_text(result,encoding='utf-8')
    else:print(result)
