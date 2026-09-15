"""CPU-only product/research routing checks. No DLL, runtime or GPU loading."""
import argparse,json,subprocess,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True,type=Path);a=p.parse_args()
r=a.candidate.resolve();assert r.is_relative_to(Path('E:/DLSSNR/builds').resolve())
out=r/'profile-policy-test';out.mkdir(exist_ok=True)
env={k.upper():v for line in subprocess.check_output(['cmd','/d','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True).splitlines() for k,sep,v in [line.partition('=')] if k and sep}
cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe';src=Path(__file__).parent/'tests/native-profile-policy.cpp';rows=[]
for profile,flag in [('release',0),('diagnostic',1)]:
 exe=out/(profile+'.exe')
 run=subprocess.run([str(cl),'/nologo','/W4','/WX','/wd4702','/EHsc','/std:c++17','/DD18_DIAGNOSTIC_BUILD='+str(flag),'/I'+str(r/'core-source/OptiScaler'),str(src.resolve()),'/Fe:'+str(exe),'/Fo:'+str(out/(profile+'.obj'))],env=env,capture_output=True)
 (out/(profile+'-build.log')).write_bytes(run.stdout+run.stderr);assert run.returncode==0
 for present in (False,True):
  case=out/(profile+('-armed' if present else '-off'));case.mkdir(exist_ok=True)
  marker=case/'D18ResearchCapture.enabled'
  if present:marker.write_bytes(b'')
  else:assert not marker.exists()
  run=subprocess.run([str(exe),str(case),str(int(present and flag))],capture_output=True,text=True);assert run.returncode==0,run.stderr
  rows.append(dict(profile=profile,marker_present=present,exit=run.returncode,output=run.stdout.strip()))
(out/'result.json').write_text(json.dumps(dict(passed=True,cases=rows,test_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),scope='CPU policy only'),indent=2),encoding='utf-8')
print(json.dumps(rows,indent=2))
