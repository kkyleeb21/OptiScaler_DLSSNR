from pathlib import Path
import json,hashlib
r=Path('E:/DLSSNR/builds/D18_Wildlands_EvaluateIsolationV8_20260913');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper();core=sha(r/'core-output/OptiScaler.dll')
isolation=json.loads((r/'isolation-results.json').read_text());normal=json.loads((r/'sr-test/results.json').read_text())
assert len(isolation)==len(normal)==1 and isolation[0]['isolation'] and not normal[0]['isolation']
for x in isolation+normal:
 assert x['exit']==0 and x['core_sha256'].upper()==core
 assert not any(e['code'] for e in x['events'])
events=isolation[0]['events'];assert any(e['stage']=='diagnostic_evaluate_only_armed' for e in events)
assert max(e.get('evaluate_done',0) for e in events)>=1500
assert not any(e.get('composed',0) or e.get('compose_done',0) or e.get('completed',0) for e in events)
assert max(e.get('completed',0) for e in normal[0]['events'])>=50
old=r.parent/'D18_Wildlands_DrawReplayV7_20260913/core-source';new=r/'core-source'
of={p.relative_to(old):p for p in old.rglob('*') if p.is_file()};nf={p.relative_to(new):p for p in new.rglob('*') if p.is_file()};assert of.keys()==nf.keys()
changed=[str(n) for n in of if sha(of[n])!=sha(nf[n])];assert set(changed)=={'OptiScaler\\dlssnr\\WildlandsSrStatus.h','OptiScaler\\menu\\menu_common.cpp','OptiScaler\\hooks\\D18WildlandsSr.inl'}
result=dict(passed=True,core_sha256=core,changed_sources=changed,unchanged_source_files=len(of)-len(changed),diagnostic_gpu_evaluations=max(e.get('evaluate_done',0) for e in events),diagnostic_compositions=0,normal_gpu_completed=max(e.get('completed',0) for e in normal[0]['events']),gameplay='not validated; crash root cause unresolved',sr_starts_disabled=True)
(r/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
