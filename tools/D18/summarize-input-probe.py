"""Summarize bounded D18 input observations without inferring input validity."""
import argparse,json,collections
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log');p.add_argument('--output',required=True);a=p.parse_args()
path=Path(a.log)
if path.stat().st_size>128*1024*1024:raise SystemExit('log exceeds bounded probe budget')
events=[];invalid=0
for line in path.read_text(encoding='utf-8-sig').splitlines():
 try:events.append(json.loads(line))
 except json.JSONDecodeError:invalid+=1
bindings=[e for e in events if e.get('event')=='input_binding']
identities=[e for e in events if e.get('event')=='input_shader_identity']
result=dict(schema='d18-input-observation-summary-v1',install=[e for e in events if e.get('event')=='input_probe_install'],present_devices=[e for e in events if e.get('event')=='input_present_device'],sessions=[e for e in events if e.get('event') in ('input_probe_armed','input_probe_end')],samples=len(bindings),roles=dict(collections.Counter(str(e['role']) for e in bindings)),invalid_lines=invalid,input_values='not_captured',depth_semantics='not_validated',motion_semantics='not_validated',sr_evaluation='not_tested',textures=sorted(set((e['role'],t['slot'],t.get('width'),t.get('height'),t.get('format')) for e in bindings for t in e['textures'] if 'width' in t)))
result['shader_inventory']=dict(records=len(identities),unique_hashes=len({e.get('hash') for e in identities}),candidate_records=sum(bool(e.get('role')) for e in identities),bindings_with_creation_tag=sum(e.get('shader_observed_at_create') is True for e in bindings),bindings_without_creation_tag=sum(e.get('shader_observed_at_create') is False for e in bindings),missing_tag_interpretation='May be early creation, uncovered creation implementation, capture budget, or tagging failure; not proof of API incompatibility')
result['coverage']=dict(stages=dict(collections.Counter(e.get('stage','PS') for e in bindings)),context_types=dict(collections.Counter(str(e.get('context_type')) for e in bindings)),unique_sampled_shaders=len({e.get('shader_hash') for e in bindings}),sweeps=[e for e in events if e['event']=='input_sweep_end'],command_lists=[e for e in events if e['event']=='input_command_lists'],complete_capture=False)
result['early_creation']=[e for e in events if e.get('event') in ('input_creation_begin','input_creation_hook','input_creation_device','input_creation_policy','input_creation_stats','input_creation_rejected','input_creation_tag_failed','input_creation_hook_limit')]
result['discovery_policy']=[e for e in events if e.get('event')=='input_discovery_policy']
focus=[e for e in events if e.get('event')=='input_focus_identity']
focus_hashes={e.get('hash') for e in focus}
result['focused_entry']={'policy':[e for e in events if e.get('event')=='input_focus_policy'],'identities':focus,'bindings':[e for e in bindings if e.get('shader_observed_at_create') and e.get('shader_hash') in focus_hashes],'interpretation':'Focused checksum matches prioritize observation only. Missing bindings may be inactive variants or sampling gaps; creation alone does not prove execution.'}
result['rendering_readiness']={'sr':'not_validated','fg':'not_validated','reason':'Shader identity and bound resource metadata alone do not validate depth, motion, jitter, exposure, or a safe colour handoff.'}
result['upstream_coverage']={s:dict(present=sum(u.get('present',False) for e in bindings for u in e.get('upstream',[]) if u['stage']==s),identified=sum(u.get('known',False) for e in bindings for u in e.get('upstream',[]) if u['stage']==s)) for s in ('VS','HS','DS','GS')}
result['shader_inventory']['stages']=dict(collections.Counter(e.get('stage','unrecorded') for e in identities))
result['coverage']['unique_identified_shaders']=len({e['shader_hash'] for e in bindings if e.get('shader_observed_at_create')})
result['numeric']={kind:[e for e in events if e.get('event')==kind] for kind in ('numeric_begin','numeric_stats','numeric_failure','numeric_pending_timeout')}
result['numeric']['issued_count']=sum(e.get('event')=='numeric_issued' for e in events)
result['numeric']['completed_count']=sum(e.get('event')=='numeric_snapshot' and e.get('complete') is True for e in events)
if result['numeric']['completed_count']:result['input_values']='sparse_numeric_capture_available_separate_archive'
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))

