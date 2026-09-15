import importlib.util,json,shutil,sys
from pathlib import Path
p=Path(__file__).with_name('analyze-layer-capture.py')
spec=importlib.util.spec_from_file_location('layer',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
root=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(exist_ok=False,parents=True)
results=[];complete=None
for manifest in sorted(root.rglob('manifest.json')):
    result,_=m.inspect(manifest.parent);results.append((str(manifest.parent),result['complete'],result['reason']))
    if result['complete'] and result['frames']==8:complete=manifest.parent
    m.write_report(result,out/manifest.parent.name)
assert complete and {r[2] for r in results}>={'complete','missing_stage','source_mode_or_recording_gap'}
bad=out/'corrupt';shutil.copytree(complete,bad)
manifest=bad/'manifest.json';original=manifest.read_bytes();doc=json.loads(original)
doc['samples'][0]['stage_mask']=1;manifest.write_text(json.dumps(doc))
try:m.inspect(bad);raise AssertionError('accepted false complete mask')
except ValueError:pass
manifest.write_bytes(original)
raw=bad/'f00_s0_r0.raw';original_raw=raw.read_bytes();raw.write_bytes(original_raw[:-1])
try:m.inspect(bad);raise AssertionError('accepted truncated raw')
except ValueError:pass
raw.write_bytes(original_raw)
doc['samples'][0]['stage_mask']=15;doc['samples'][1]['source']=999;manifest.write_text(json.dumps(doc))
try:m.inspect(bad);raise AssertionError('accepted false sequence continuity')
except ValueError:pass
manifest.write_bytes(original)
(out/'result.json').write_text(json.dumps({'valid_fixtures':results,'rejected':['false_complete_mask','truncated_raw','false_sequence_continuity']},indent=2))
print('PASS complete/partial/source-change/half-float reports; false complete, truncated raw and false continuity rejected')
