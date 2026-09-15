"""Run and archive a retained candidate's SR/NR GPU integration fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

def archive(candidate, output, case):
    dest=output/case;dest.mkdir(parents=True,exist_ok=True)
    source=candidate/'sr-test/offscreen-handoff'
    for p in source.iterdir():
        if case=='failure' and p.name=='D24Native.log': continue
        if p.is_file() and p.suffix.lower() in ('.log','.jsonl','.txt','.ini'):
            shutil.copy2(p,dest/p.name)
    result_name={'same-size':'same-size-results.json','resize':'resize-results.json','failure':'failure-results.json'}[case]
    if (candidate/'sr-test'/result_name).exists():shutil.copy2(candidate/'sr-test'/result_name,dest/'results.json')
    identities={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (source/'dxgi.dll',source/'D24Native.dll') if p.exists()}
    (dest/'identity.json').write_text(json.dumps(identities,indent=2),encoding='utf-8')
    return dest

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--case',choices=['same-size','resize','failure'],required=True)
    a=p.parse_args()
    flags={'same-size':['--nr','--same-size'],'resize':['--nr','--resize'],'failure':['--nr-failure','--same-size']}[a.case]
    run=subprocess.run([sys.executable,str(a.candidate/'test-sr.py'),*flags,'offscreen-handoff'],capture_output=True,timeout=360)
    dest=archive(a.candidate,a.output,a.case)
    (dest/'runner.txt').write_bytes(run.stdout+run.stderr)
    if (dest/'D24Native.log').exists():
        subprocess.run([sys.executable,str(Path(__file__).with_name('summarize-native-nr.py')),str(dest/'D24Native.log'),'--execution',str(dest/'D18ExecutionTrace.jsonl'),'--output',str(dest/'native-nr-summary.json')],check=True)
    print(json.dumps(dict(case=a.case,exit=run.returncode,evidence=str(dest))))
    raise SystemExit(run.returncode)
