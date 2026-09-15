"""Compile/run the retained native FG present policy; no game or GPU access."""
import argparse,subprocess,json,hashlib
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--candidate',type=Path,required=True);a=parser.parse_args()
r=a.candidate.resolve()
if not r.is_relative_to(Path('E:/DLSSNR/builds').resolve()):raise SystemExit('Candidate must be under builds')
env={}
for line in subprocess.check_output(['cmd','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True).splitlines():
 k,sep,v=line.partition('=')
 if k and sep:env[k.upper()]=v
out=r/'policy-test';out.mkdir(exist_ok=True)
cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
source=Path(__file__).parent/'tests/native-fg-present-policy.cpp'
args=[str(cl),'/nologo','/W4','/WX','/EHsc','/std:c++17','/I'+str(r/'core-source/OptiScaler'),str(source.resolve()),'/Fe:'+str(out/'policy.exe'),'/Fo:'+str(out/'policy.obj')]
run=subprocess.run(args,env=env,capture_output=True,text=True);(out/'build.log').write_text(run.stdout+run.stderr,encoding='utf-8')
if run.returncode:raise SystemExit(run.returncode)
run=subprocess.run([str(out/'policy.exe')],capture_output=True,text=True)
(out/'result.txt').write_text(run.stdout+run.stderr,encoding='utf-8')
header=r/'core-source/OptiScaler/dlssnr/NativeFgPresentPolicy.h'
(out/'result.json').write_text(json.dumps({'exit':run.returncode,'policy_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'test_sha256':hashlib.sha256(source.read_bytes()).hexdigest()},indent=2),encoding='utf-8')
print(run.stdout);raise SystemExit(run.returncode)
