"""Create a complete successor with a diagnostic-only pass-boundary Reset host."""
from pathlib import Path
import argparse,hashlib,json,shutil
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
assert not a.output.exists()
origin=a.baseline/'core-source/tools/D18/probe-reset-isolation-runtime.cpp'
s=origin.read_text(encoding='utf-8-sig')
def rep(old,new):
 global s
 assert s.count(old)==1,old
 s=s.replace(old,new)
rep('resetPolicy==L"initial"||resetPolicy==L"every_call"','resetPolicy==L"initial"||resetPolicy==L"every_call"||resetPolicy==L"first_pass"||resetPolicy==L"second_pass"')
rep('const bool reset=resetEveryCall||(frame==0&&(!shared||pass==0));','const bool reset=resetEveryCall||(resetPolicy==L"first_pass"&&pass==0)||(resetPolicy==L"second_pass"&&pass==1)||(frame==0&&(!shared||pass==0));')
rep('if(frame==0)printf("CALL pass=%u feature=%u params=%u reset=%u frozen=%u\\n",pass,instance,paramIndex,unsigned(reset),unsigned(frozen&&pass==1));',
    'printf("BOUNDARY frame=%u pass=%u feature=%u params=%u requested_reset=%u frozen=%u\\n",frame,pass,instance,paramIndex,unsigned(reset),unsigned(frozen&&pass==1));')
a.output.mkdir(parents=True);shutil.copytree(a.baseline/'core-source',a.output/'core-source')
shutil.copy2(a.baseline/'run-multipass-checks.py',a.output/'run-multipass-checks.py')
target=a.output/'core-source/tools/D18/probe-reset-boundary-runtime.cpp';target.write_text(s,encoding='utf-8')
(a.output/'preparation.json').write_text(json.dumps(dict(baseline=str(a.baseline.resolve()),source=str(origin.resolve()),source_sha256=hashlib.sha256(origin.read_bytes()).hexdigest().upper(),scope='Diagnostic host only; first/second-pass Reset partitions; 128 bounded requested-reset observations, not internal-reset observations; no production changes'),indent=2),encoding='utf-8')
print(target)
