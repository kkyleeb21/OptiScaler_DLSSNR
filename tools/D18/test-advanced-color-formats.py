"""Compile/run bounded owned-host colour-format checks; no game or NVIDIA runtime loading."""
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--backend',choices=['warp','nvidia'],default='warp');a=p.parse_args()
s=a.candidate.resolve()/'core-source';out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
raw=subprocess.check_output(['cmd.exe','/d','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True)
env={k.upper():v for line in raw.splitlines() for k,sep,v in [line.partition('=')] if k and sep}
compiler=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
results=[]
for name in ['test-advanced-color-policy','test-multipass-color-formats','test-highres-pixels-fp16','test-multipass-pixels-fp16']:
 exe=out/(name+'.exe')
 cmd=[str(compiler),'/nologo','/std:c++20','/EHsc','/W4','/WX','/wd4324','/D_CRT_SECURE_NO_WARNINGS','/O2','/UNDEBUG','/I'+str(s/'OptiScaler'),str(s/'tools/D18'/(name+'.cpp')),'/Fe:'+str(exe),'/Fo:'+str(out/(name+'.obj')),'/link','d3d12.lib','dxgi.lib','d3dcompiler.lib']
 with (out/(name+'-build.log')).open('wb') as f:subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
 args=[str(exe)]+(['--nvidia'] if a.backend=='nvidia' and name!='test-advanced-color-policy' else [])
 with (out/(name+'.log')).open('wb') as f:subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
 results.append(dict(test=name,passed=True,backend='cpu' if name=='test-advanced-color-policy' else a.backend))
(out/'result.json').write_text(json.dumps(dict(passed=True,tests=results,game_executed=False,real_model_loaded=False,scope='Production orchestration and shaders; format storage/alpha preservation, failure rollback, signed FP16 residuals, explicit request recovery'),indent=2),encoding='utf-8')
print('PASS bounded colour-format checks; no game or real model loaded')
