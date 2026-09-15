"""Validate owned-host traces; requested/effective/reset-body observations stay distinct."""
from pathlib import Path
import argparse,collections,hashlib,json,re
p=argparse.ArgumentParser();p.add_argument('runs',type=Path,nargs='+');p.add_argument('--reference',type=Path,action='append',default=[]);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
assert not a.output.exists() and (not a.reference or len(a.reference)==len(a.runs))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
summaries=[]
for i,run in enumerate(a.runs):
 m=json.loads((run/'manifest.json').read_text());trace=run/'runtime-state.jsonl';assert trace.stat().st_size<=1024*1024
 assert m['complete'] and m['observer_complete'] and m['observer_trace_sha256']==sha(trace)
 records=[json.loads(l) for l in trace.read_text().splitlines()];assert records[0]['schema'] in ['d18-nr-state-observer-v1','d18-nr-state-observer-v2','d18-nr-state-observer-v3']
 assert not records[0].get('bindings_phase') and not records[0].get('resources_phase') and not records[0].get('data_modified'),'Use the matching phase/intervention adapter'
 assert records[-1]['event']=='exit' and records[-1]['code']==0 and not any(r['event'] in ['exception','observer_failed'] for r in records)
 points={k:[r for r in records if r.get('point')==k] for k in range(4)}
 for k in [0,1,3]:assert [r['call'] for r in points[k]]==list(range(128))
 requested=[tuple(map(int,v)) for v in re.findall(r'BOUNDARY frame=(\d+) pass=(\d+) feature=(\d+) params=(\d+) requested_reset=(\d+) frozen=(\d+)',(run/'probe.log').read_text())]
 assert len(requested)==128 and 'debug_layer=1' in (run/'probe.log').read_text()
 for call,v in enumerate(requested):assert v[:2]==divmod(call,2)
 before=[r['reset_before_control'] for r in points[0]];effective=[r['effective_reset_at_gate'] for r in points[1]]
 assert [r['call'] for r in points[2] if r['call']>=0]==[n for n,v in enumerate(effective) if v],'effective Reset/body pairing'
 for name,h in m['files'].items():assert sha(run/name)==h
 row=dict(run=str(run.resolve()),schema=records[0]['schema'],phase='state' if records[0].get('state_phase') else 'controls',call_count=128,requested_resets=sum(bool(v[4]) for v in requested),before_control_resets=sum(bool(v) for v in before),effective_resets=sum(bool(v) for v in effective),request_to_precontrol_changes=[n for n in range(128) if bool(requested[n][4])!=bool(before[n])],control_to_gate_changes=[n for n in range(128) if bool(before[n])!=bool(effective[n])],pre_evaluate_body_hits=sum(r['call']==-1 for r in points[2]),evaluate_body_hits=sum(r['call']>=0 for r in points[2]),trace_sha256=sha(trace),manifest_sha256=sha(run/'manifest.json'),host_sha256=m['executable_sha256'],observer_sha256=m['observer_sha256'])
 if row['phase']=='controls':row['reset_target_rvas']=sorted({hex(r['target_rva_if_in_runtime']) for r in points[2]})
 else:
  pointers=sorted({v['address'] for r in points[0] for v in r['states']});rows=[]
  for addr in pointers:
   entry=[next(v['value'] for v in r['states'] if v['address']==addr) for r in points[0]]
   after=[next(v['value'] for v in r['states'] if v['address']==addr) for r in points[3]]
   rows.append(dict(session_local_address=addr,before_control=entry,after_reset_gate=after,clear_calls=[dict(call=r['call'],before=r['value_before_clear']) for r in points[2] if r['state']==addr]))
  row['states']=rows
  active={r['state'] for r in points[2] if r['call']>=0}
  assert len(active)==1,'This two-instance fixture expects one active state'
  for state in rows:
   for n,(before_value,after_value) in enumerate(zip(state['before_control'],state['after_reset_gate'])):
    expected=0 if effective[n] and state['session_local_address'] in active else before_value
    assert after_value==expected,'Observed state at Reset boundary differs'
 if a.reference:
  ref=json.loads((a.reference[i]/'manifest.json').read_text());assert ref['complete']
  for key in ['runtime_sha256','driver_core_sha256','executable_sha256','mode','reset_policy','ratio','style','frozen_input_sha256']:assert m[key]==ref[key],key
  assert set(m['files'])==set(ref['files'])
  for n,h in ref['files'].items():assert sha(a.reference[i]/n)==h
  row['reference']=str(a.reference[i].resolve());row['exact_files']=sum(h==ref['files'][n] for n,h in m['files'].items());row['compared_files']=len(m['files']);assert row['exact_files']==row['compared_files']
 summaries.append(row)
a.output.parent.mkdir(exist_ok=True,parents=True);a.output.write_text(json.dumps(dict(schema='d18-nr-state-summary-v1',runs=summaries,boundary='Main-thread owned-host observations only. Effective Reset at one audited gate is not all runtime state. CPU counter progression is not GPU completion. Debugger perturbs scheduling; exact pixel controls bound this fixture, not game quality.'),indent=2),encoding='utf-8')
for r in summaries:print(Path(r['run']).name,'requested/effective',r['requested_resets'],r['effective_resets'],'pre-evaluate/evaluate body hits',r['pre_evaluate_body_hits'],r['evaluate_body_hits'],'exact',r.get('exact_files'))
