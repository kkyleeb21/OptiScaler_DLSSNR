"""Bounded synthetic single-versus-neutral-second-pass resolve diagnosis; no NGX/game."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--backend',choices=['warp','nvidia'],default='warp');p.add_argument('--all-mode-hf',action='store_true');a=p.parse_args()
s=a.candidate.resolve()/'core-source';out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
raw=subprocess.check_output(['cmd.exe','/d','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True)
env={k.upper():v for line in raw.splitlines() for k,sep,v in [line.partition('=')] if k and sep}
compiler=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe';name='test-neutral-pass-compose';exe=out/(name+'.exe')
cmd=[str(compiler),'/nologo','/std:c++20','/EHsc','/W4','/WX','/wd4324','/D_CRT_SECURE_NO_WARNINGS','/O2','/UNDEBUG','/I'+str(s/'OptiScaler'),str(Path(__file__).with_suffix('.cpp')),'/Fe:'+str(exe),'/Fo:'+str(out/(name+'.obj')),'/link','d3d12.lib','dxgi.lib','d3dcompiler.lib']
if a.all_mode_hf:cmd.insert(1,'/DD18_ALL_MODE_HF=1')
with (out/'build.log').open('wb') as f:subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
result=subprocess.run([str(exe)]+(['--nvidia'] if a.backend=='nvidia' else []),capture_output=True,text=True,timeout=45)
(out/'pixels.log').write_text(result.stdout+result.stderr,encoding='utf-8');print(result.stdout)
(out/'result.json').write_text(json.dumps(dict(passed=result.returncode==0,backend=a.backend,real_model_loaded=False,game_executed=False,scope='Synthetic nonidentity first model and identity second model; FP32 inputs isolate final-composition policy; not captured game pixels'),indent=2),encoding='utf-8')
raise SystemExit(result.returncode)
