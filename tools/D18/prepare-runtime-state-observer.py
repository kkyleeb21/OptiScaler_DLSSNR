"""Prepare a complete successor plus an owned-host hardware-breakpoint observer."""
from pathlib import Path
import argparse,hashlib,json,shutil
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
assert not a.output.exists()
host=a.baseline/'tests/probe-reset-boundary-runtime.exe';assert host.is_file()
observer=Path(__file__).with_name('nr-state-debugger.cpp')
assert not (a.baseline/'core-source/tools/D18/nr-state-debugger.cpp').exists(),'Choose a successor without an existing observer or explicitly review that change'
a.output.mkdir(parents=True);shutil.copytree(a.baseline/'core-source',a.output/'core-source');shutil.copy2(a.baseline/'run-multipass-checks.py',a.output/'run-multipass-checks.py')
shutil.copy2(observer,a.output/'core-source/tools/D18/nr-state-debugger.cpp');(a.output/'tests').mkdir();shutil.copy2(host,a.output/'tests'/host.name)
(a.output/'preparation.json').write_text(json.dumps(dict(baseline=str(a.baseline.resolve()),scope='complete source successor; add observer only; unchanged boundary host reused',host_exe_reused=str(host.resolve()),host_sha256=hashlib.sha256(host.read_bytes()).hexdigest().upper(),observer_source_sha256=hashlib.sha256(observer.read_bytes()).hexdigest().upper()),indent=2),encoding='utf-8')
print(a.output)
