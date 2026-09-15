"""CPU checks for disabled research allocations and retained safety metadata."""
import argparse,subprocess,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--candidate',type=Path,required=True);a=p.parse_args()
r=a.candidate.resolve();out=r/'command-trace-gate-test';out.mkdir(exist_ok=True)
env={k.upper():v for line in subprocess.check_output(['cmd','/d','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True).splitlines() for k,sep,v in [line.partition('=')] if k and sep}
cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe';rows=[]
for profile,flag in [('release',0),('diagnostic',1)]:
 exe=out/(profile+'.exe')
 q=subprocess.run([str(cl),'/nologo','/W4','/WX','/wd4702','/EHsc','/std:c++17','/DD18_DIAGNOSTIC_BUILD='+str(flag),'/I'+str(r/'core-source/OptiScaler'),str((Path(__file__).parent/'tests/command-trace-gate.cpp').resolve()),'/Fe:'+str(exe),'/Fo:'+str(out/(profile+'.obj'))],env=env,capture_output=True)
 (out/(profile+'-build.log')).write_bytes(q.stdout+q.stderr);assert q.returncode==0
 for armed in (False,True):
  case=out/(profile+('-armed' if armed else '-off'));case.mkdir(exist_ok=True)
  if armed:(case/'D18ResearchCapture.enabled').write_text('fixture')
  q=subprocess.run([str(exe),str(case),str(int(flag and armed))],capture_output=True,text=True)
  rows.append(dict(profile=profile,armed=armed,exit=q.returncode,output=q.stdout,stderr=q.stderr));assert q.returncode==0,rows[-1]
(out/'result.json').write_text(json.dumps(dict(passed=True,cases=rows,scope='CPU only; no game/GPU execution'),indent=2))
print('PASS: release and diagnostic x marker absent/present; production metadata retained')
