from pathlib import Path
import json,hashlib
r=Path('E:/DLSSNR/builds/D18_Wildlands_WritebackIsolationV10_20260913');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper();core=sha(r/'core-output/OptiScaler.dll')
rows=json.loads((r/'stage-test/results.json').read_text());normal=json.loads((r/'sr-test/results.json').read_text())
assert {x['mode'] for x in rows}=={'stage7','stage8','stage5','stage-cycle'}
assert len(normal)==1 and normal[0]['mode']=='draw6'
for x in rows+normal:
 assert x['exit']==0 and x['core_sha256'].upper()==core
 assert not any(e['code'] for e in x['events'])
for x in rows:
 assert not any(e['stage']=='created' or e.get('prepare_done',0) or e.get('evaluate_done',0) or e.get('composed',0) for e in x['events'])
cycle=next(x for x in rows if x['mode']=='stage-cycle');assert cycle['applied']==[7,8,5,7,8,5,0,7]
assert all(cycle['gpu'][str(i)]>0 for i in (5,7,8))
old=r.parent/'D18_Wildlands_StagePanelV9_20260913/core-source';new=r/'core-source'
of={p.relative_to(old):p for p in old.rglob('*') if p.is_file()};nf={p.relative_to(new):p for p in new.rglob('*') if p.is_file()};assert of.keys()==nf.keys()
changed=[str(n) for n in of if sha(of[n])!=sha(nf[n])];assert set(changed)=={'OptiScaler\\dlssnr\\SrDiagnosticStages.h','OptiScaler\\menu\\menu_common.cpp','OptiScaler\\hooks\\D18WildlandsSr.inl'}
result=dict(passed=True,core_sha256=core,changed_sources=changed,unchanged_source_files=len(of)-len(changed),cycle_applied=cycle['applied'],cycle_gpu=cycle['gpu'],normal_gpu_completed=max(e.get('completed',0) for e in normal[0]['events']),gameplay='unverified; writeback isolation, not a crash fix',sr_starts_disabled=True)
(r/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
