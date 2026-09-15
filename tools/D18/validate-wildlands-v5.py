from pathlib import Path
import json,hashlib
r=Path('E:/DLSSNR/builds/D18_Wildlands_SRContractsV5_20260913')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
core=sha(r/'core-output/OptiScaler.dll')
sr=json.loads((r/'sr-test/results.json').read_text())
contracts=json.loads((r/'contract-test/sr-test/results.json').read_text())
assert {x['mode'] for x in sr}=={'ui-transition','ui-release'}
assert {x['mode'] for x in contracts}=={'scissor','mask'}
for x in sr+contracts:
 assert x['exit']==0 and x['core_sha256'].upper()==core,x['mode']
 assert not any(e['code'] for e in x['events'])
state=json.loads((r/'state-test/results.json').read_text());assert state['exit']==0
old=r.parent/'D18_Wildlands_ExecutionV4_20260913/core-source'
new=r/'core-source'
oldFiles={p.relative_to(old):p for p in old.rglob('*') if p.is_file()}
newFiles={p.relative_to(new):p for p in new.rglob('*') if p.is_file()}
assert oldFiles.keys()==newFiles.keys(),'Full source inheritance mismatch'
changed=[str(n) for n in oldFiles if sha(oldFiles[n])!=sha(newFiles[n])]
allowed={'OptiScaler\\hooks\\D18WildlandsSr.inl','OptiScaler\\hooks\\D18InputProbe.h','OptiScaler\\wrapped\\wrapped_swapchain.cpp','OptiScaler\\dlssnr\\WildlandsSrStatus.h','OptiScaler\\menu\\menu_common.cpp'}
assert set(changed)==allowed,changed
result=dict(passed=True,core_sha256=core,changed_sources=changed,unchanged_source_files=len(oldFiles)-len(changed),
 sr=[dict(mode=x['mode'],gpu_completed=max(e.get('completed',0) for e in x['events'])) for x in sr],
 output_guards=['scissor-outside pixels unchanged','masked green channel unchanged'],
 state_test=state,gameplay='not yet validated',sr_starts_disabled=True)
(r/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
