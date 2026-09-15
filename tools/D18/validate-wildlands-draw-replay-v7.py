from pathlib import Path
import hashlib,json
r=Path('E:/DLSSNR/builds/D18_Wildlands_DrawReplayV7_20260913')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
core=sha(r/'core-output/OptiScaler.dll')
sr=json.loads((r/'sr-test/results.json').read_text());contracts=json.loads((r/'contract-test/sr-test/results.json').read_text())
assert {x['mode'] for x in sr}=={'draw6','ui-transition','ui-release'}
assert {x['mode'] for x in contracts}=={'scissor','mask'}
for x in sr+contracts:
 assert x['exit']==0 and x['core_sha256'].upper()==core
 assert not any(e['code'] for e in x['events'])
state=json.loads((r/'draw-replay-test/results.json').read_text());assert state['exit']==0 and state['stdout'].count('wrong_pixels=0')==5
old=r.parent/'D18_Wildlands_CoverageV6_20260913/core-source';new=r/'core-source'
oldfiles={p.relative_to(old):p for p in old.rglob('*') if p.is_file()};newfiles={p.relative_to(new):p for p in new.rglob('*') if p.is_file()}
assert oldfiles.keys()<=newfiles.keys()
changed=[str(n) for n in oldfiles if sha(oldfiles[n])!=sha(newfiles[n])]
assert set(changed)=={'OptiScaler\\hooks\\D18WildlandsSr.inl','OptiScaler\\hooks\\D18InputProbe.h'},changed
added=[str(n) for n in newfiles.keys()-oldfiles.keys()];assert added==['OptiScaler\\dlssnr\\Dx11DrawReplay.h'],added
result=dict(passed=True,core_sha256=core,changed_sources=changed,added_sources=added,unchanged_source_files=len(oldfiles)-len(changed),sr=[dict(mode=x['mode'],gpu_completed=max(e.get('completed',0) for e in x['events'])) for x in sr],geometry_test=state,output_guards=['scissor','RGB write mask'],geometry_policy='Replay original IA/VS/raster and direct draw parameters; no count whitelist',gameplay='not yet validated',sr_starts_disabled=True)
(r/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
