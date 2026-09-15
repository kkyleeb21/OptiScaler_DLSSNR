"""Offline evidence rejection checks. No GPU execution or game access."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,shutil,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--work-dir',required=True,type=Path);p.add_argument('--bindings',required=True,type=Path);p.add_argument('--counter',required=True,type=Path);p.add_argument('--baseline',required=True,type=Path);p.add_argument('--independent',required=True,type=Path);p.add_argument('--legacy-controls',required=True,type=Path);a=p.parse_args()
assert not a.work_dir.exists();a.work_dir.mkdir(parents=True)
tools=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,tools/(name+'.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
bindings=module('summarize-nr-runtime-bindings');counter=module('summarize-nr-counter-isolation')
passed=[]
def clone(source,name,change):
    dest=a.work_dir/name;dest.mkdir()
    m=json.loads((source/'manifest.json').read_text(encoding='utf-8'))
    for n in m['files']:os.link(source/n,dest/n)
    shutil.copy2(source/'probe.log',dest/'probe.log')
    rows=[json.loads(v) for v in (source/'runtime-state.jsonl').read_text(encoding='utf-8').splitlines()]
    change(rows)
    trace=dest/'runtime-state.jsonl';trace.write_text(''.join(json.dumps(r)+'\n' for r in rows),encoding='utf-8')
    m['observer_trace_sha256']=hashlib.sha256(trace.read_bytes()).hexdigest().upper()
    (dest/'manifest.json').write_text(json.dumps(m),encoding='utf-8')
    return dest
def reject(name,action):
    try:action()
    except AssertionError:passed.append(name);return
    raise RuntimeError(name+' was incorrectly accepted')
misdirected=clone(a.bindings,'misdirected-history',lambda rows:next(r for r in rows if r.get('point')==3).__setitem__('destination_resource',0))
reject('misdirected history copy',lambda:bindings.summarize(misdirected,a.baseline))
bad_counter=clone(a.counter,'counter-mismatch',lambda rows:next(r for r in rows if r.get('point')==1).__setitem__('counter_after_intervention',999))
reject('counter write/readback mismatch',lambda:counter.summarize(bad_counter,a.baseline,a.independent))
reject('intervention rejected by read-only adapter',lambda:bindings.summarize(a.counter,a.baseline))
legacy=clone(a.legacy_controls,'legacy-v3-envelope',lambda rows:rows[0].__setitem__('schema','d18-nr-state-observer-v3'))
q=subprocess.run([sys.executable,str(tools/'summarize-nr-runtime-state.py'),str(legacy),'--reference',str(a.baseline),'--output',str(a.work_dir/'legacy-v3-summary.json')],capture_output=True,text=True)
assert q.returncode==0,q.stdout+q.stderr;passed.append('controls payload accepted under v3 envelope')
(a.work_dir/'result.json').write_text(json.dumps(dict(schema='d18-runtime-evidence-adapter-tests-v1',passed=passed,gpu_executed=False,scope='Mutated offline trace fixtures only; manifests rehashed so semantic checks must reject invalid evidence.'),indent=2),encoding='utf-8')
print('PASS',', '.join(passed))
