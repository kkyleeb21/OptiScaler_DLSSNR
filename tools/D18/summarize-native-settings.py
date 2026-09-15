"""Summarize bounded native-setting snapshots; chronology is not call-chain proof."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('log');p.add_argument('--output',required=True)
a=p.parse_args();path=Path(a.log);data=path.read_bytes()
if len(data)>1024*1024:raise SystemExit('Log exceeds size budget')
rows=[json.loads(x) for x in data.decode('utf-8-sig').splitlines()]
if len(rows)>261:raise SystemExit('Log exceeds record budget')
if any(x.get('event')=='native_scale' for x in rows):
 events=[x for x in rows if x.get('event')=='native_scale']
 generations={}
 for x in events:
  key=str(x['generation']);g=generations.setdefault(key,{'stages':[],'native_finished':False,'input_observed':False,'sr_gpu_completed':False})
  g['stages'].append(x['stage']);g['last']=x
  if x['stage']=='native_callback_finished':g['native_finished']=True
  if x['stage']=='engine_input_observed':g['input_observed']=True
  if x['stage']=='sr_gpu_completed':g['sr_gpu_completed']=True
 for x in rows:
  if x.get('event')=='native_scale_context':generations.setdefault(str(x['generation']),{}).setdefault('contexts',[]).append(x)
 result=dict(schema='d18-native-scale-summary-v1',sha256=hashlib.sha256(data).hexdigest(),
  records=len(events),context_records=sum(x.get('event')=='native_scale_context' for x in rows),at_record_limit=sum(x.get('event') in ('native_scale','native_scale_context') for x in rows)>=128,adapter_observed=any(x['stage']=='engine_queue_adapter_ready' for x in events),
  generations=generations,boundary='Native callback, observed input dimensions and SR GPU completion are separate evidence. Missing events are not proof of failure; capped logs do not establish whole-session stability or visual quality.')
 Path(a.output).write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2));raise SystemExit(0)
if any(x.get('event')=='trace_armed' for x in rows):
 arm=next(x for x in rows if x.get('event')=='trace_armed')
 hits=[x for x in rows if x.get('event')=='native_apply_hit']
 ends=[x for x in rows if x.get('event')=='trace_end']
 result=dict(schema='d18-native-apply-summary-v1',sha256=hashlib.sha256(data).hexdigest(),pid=arm['pid'],target_rva=arm['rva'],
  hit_count=len(hits),hits=hits,detached=bool(ends and ends[-1]['detach_hresult']==0),
  target_pc_matches=bool(len(hits)==1 and hits[0]['stack'] and hits[0]['stack'][0]==arm['base']+arm['rva']),
  errors=[x for x in rows if x.get('event')=='trace_error'],
  boundary='Observed native entry thread, RCX and bounded stack only. Does not prove a safe callback for new calls or resource rebuild completion.')
 Path(a.output).write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2));raise SystemExit(0)
snap=[x for x in rows if x['event']=='settings_snapshot']
if any(x.get('slot') not in (0,1,2) or len(x.get('bytes',''))!=416 for x in snap):raise SystemExit('Invalid snapshot')
slots={}
for slot in range(3):
 events=[x for x in snap if x['slot']==slot]
 slots[str(slot)]={'rva':events[0]['rva'] if events else None,'samples':len(events),'scale_timeline':[{'tick':x['tick'],'scale':x['scale_field'],'dimensions':[x['width_field'],x['height_field']]} for x in events]}
result={'schema':'d18-native-settings-summary-v1','sha256':hashlib.sha256(data).hexdigest(),'complete':bool(rows and rows[-1]['event']=='settings_watch_end'),'at_limit':rows[-1].get('at_limit') if rows else None,'slots':slots,'errors':[x for x in rows if x['event']=='read_failed'],'boundary':'Settings-object values and sampled chronology only. Does not identify an apply function, owning thread, actual render-target dimensions or SR output.'}
result['render_settings_probes']=[x for x in rows if x['event']=='render_settings_probe']
result['render_probe_boundary']='A render-settings field and window-owner thread are not actual RT dimensions or the native Apply caller thread.'
Path(a.output).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
