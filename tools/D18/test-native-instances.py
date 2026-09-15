"""Build/run bounded real-NR FL11.0 feature ownership and temporal isolation host."""
import argparse,hashlib,json,os,shutil,subprocess,time,array,math
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True,type=Path);p.add_argument('--runtime-dir',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--chain',action='store_true');p.add_argument('--formats',action='store_true');p.add_argument('--failure',action='store_true');a=p.parse_args()
r=a.candidate.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
src=r/'native-source/bg3-native';core=r/'core-source/OptiScaler'
sdk=Path('E:/DLSSNR/workspace/dlss5/worktrees/optiscaler-internal-scaling/external/nvngx_dlss_sdk')
identities={}
for name in ['D24Runtime.dll','nvngx_dlssnr.dll']:
    f=a.runtime_dir/name;shutil.copy2(f,out/name);identities[name]=hashlib.sha256(f.read_bytes()).hexdigest()
host=Path(__file__).with_name('native-instance-host.cpp').resolve()
command=f'@echo off\ncall "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /O2 /EHsc /std:c++20 /I"{src}" /I"{core}" /I"{sdk}" "{host}" /Fo"{out}/host.obj" /Fe"{out}/host.exe"\n'
(out/'build.cmd').write_text(command,encoding='utf-8')
env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['Path']=os.environ['PATH']
build=subprocess.run(['cmd.exe','/d','/c',str(out/'build.cmd')],capture_output=True,env=env,cwd=out)
(out/'build.log').write_bytes(build.stdout+build.stderr)
if build.returncode: print((build.stdout+build.stderr).decode(errors='replace'));raise SystemExit(build.returncode)
results=dict(scope='real NR, FL11.0 isolated host; not gameplay or multipass composition',runtime=identities,cases=[])
cases=['failure'] if a.failure else [f'chain-{fmt}' for fmt in [2,10,26,28,24,11,87]] if a.formats else ['chain'] if a.chain else ['single','dual']
for case in cases:
    start=time.monotonic()
    with (out/f'{case}.log').open('wb') as f:
        try: run=subprocess.run([str(out/'host.exe'),*case.split('-')],stdout=f,stderr=subprocess.STDOUT,cwd=out,timeout=120);code=run.returncode
        except subprocess.TimeoutExpired: code='timeout'
    if (out/'D24Native.log').exists():shutil.copy2(out/'D24Native.log',out/f'{case}-native.log')
    results['cases'].append(dict(case=case,exit=code,seconds=time.monotonic()-start));print(case,code,flush=True)
    if code and not(a.formats and code==77):break
results['passed']=len(results['cases'])==len(cases) and all(v['exit']==0 or (a.formats and v['exit']==77) for v in results['cases'])
if results['passed'] and not (a.chain or a.formats or a.failure):
    x=array.array('f');x.frombytes((out/'single.f32').read_bytes());y=array.array('f');y.frombytes((out/'dual.f32').read_bytes())
    results['pixels_equal_length']=len(x)==len(y)
    results['max_abs_difference']=max(abs(v-w) for v,w in zip(x,y))
    results['passed'] &= len(x)==len(y) and results['max_abs_difference']<=1e-6
results['source_sha256']=hashlib.sha256((src/'D24Native.cpp').read_bytes()).hexdigest()
results['advanced_source_sha256']=hashlib.sha256((src/'native_advanced.h').read_bytes()).hexdigest()
if a.chain or a.formats:results['scope']='real NR FL11.0 host: independent 2/3/4 passes, shared 4, highres 1.25/1.5, ordinary single and return to two; format exit77 is unsupported, not executed; not gameplay'
if a.failure:results['scope']='real NR FL11.0 host, partial allocation failure before model dispatch, exact FP32 SR preservation and recovery; not device-loss or gameplay'
(out/'result.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print(json.dumps(results));raise SystemExit(0 if results['passed'] else 1)
