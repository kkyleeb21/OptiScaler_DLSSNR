from pathlib import Path
import hashlib,json
r=Path('E:/DLSSNR/builds/D18_Wildlands_CoverageV6_20260913')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
core=sha(r/'core-output/OptiScaler.dll')
sr=json.loads((r/'sr-test/results.json').read_text())
contracts=json.loads((r/'contract-test/sr-test/results.json').read_text())
assert {x['mode'] for x in sr}=={'ui-transition'}
assert {x['mode'] for x in contracts}=={'scissor','mask'}
for x in sr+contracts:
 assert x['exit']==0 and x['core_sha256'].upper()==core
 assert not any(e['code'] for e in x['events'])
for mode,reason in [('scissor',128),('mask',1024)]:
 summary=json.loads((r/f'contract-test/sr-test/{mode}/summary.json').read_text())
 snapshots=summary['execution']['coverage_snapshots']
 assert 1<=len(snapshots)<=8
 for s in snapshots:
  assert s['reason_mask']==reason and s['draw']=={'count':3,'first':0}
  assert s['dimensions']==[128,128] and len(s['raw'])==7
  if mode=='scissor':assert s['scissor_rect']==[0,0,64,128]
  else:assert s['blend']['write_mask']==9
 assert not summary['gpu_completed_frames'] and not summary['recorded_composed_frames']
old=r.parent/'D18_Wildlands_SRContractsV5_20260913/core-source'
new=r/'core-source'
oldfiles={p.relative_to(old):p for p in old.rglob('*') if p.is_file()}
newfiles={p.relative_to(new):p for p in new.rglob('*') if p.is_file()}
assert oldfiles.keys()==newfiles.keys()
changed=[str(n) for n in oldfiles if sha(oldfiles[n])!=sha(newfiles[n])]
assert set(changed)=={'OptiScaler\\hooks\\D18WildlandsSr.inl','OptiScaler\\hooks\\D18InputProbe.h','OptiScaler\\dlssnr\\WildlandsSrStatus.h','OptiScaler\\menu\\menu_common.cpp'},changed
result=dict(passed=True,core_sha256=core,changed_sources=changed,unchanged_source_files=len(oldfiles)-len(changed),sr=[dict(mode=x['mode'],gpu_completed=max(e.get('completed',0) for e in x['events'])) for x in sr],coverage_decoder_verified=['scissor bit 128 with rectangle','RGB mask bit 1024 with mask 9'],coverage_policy='V5 unchanged; diagnostics only',gameplay='not yet validated',sr_starts_disabled=True)
(r/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
