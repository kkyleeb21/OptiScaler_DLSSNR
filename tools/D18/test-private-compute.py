"""Reusable DX11.0/11.1 private GPU constant/scalar preparation fixture."""
from pathlib import Path
import subprocess,json,argparse,hashlib
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
source=a.source_root.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert (source/'dlssnr/Dx11PrivateCompute.h').is_file()
env={}
for line in subprocess.check_output(['cmd','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True).splitlines():
 k,sep,v=line.partition('=')
 if k and sep:env[k.upper()]=v
cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe';test=Path(__file__).resolve().parent/'tests/private-compute-host.cpp'
s=subprocess.run([str(cl),'/nologo','/W4','/WX','/EHsc','/std:c++17','/I'+str(source),str(test),'/Fe:'+str(out/'compute-test.exe'),'/Fo:'+str(out/'compute-test.obj')],env=env,capture_output=True,text=True)
(out/'compute-test-build.log').write_text(s.stdout+s.stderr,encoding='utf-8');assert s.returncode==0,s.stdout+s.stderr
s=subprocess.run([str(out/'compute-test.exe')],capture_output=True,text=True,timeout=60)
result=dict(schema='d18-private-compute-fixture-v1',exit=s.returncode,stdout=s.stdout,stderr=s.stderr,source_sha256=hashlib.sha256((source/'dlssnr/Dx11PrivateCompute.h').read_bytes()).hexdigest(),fixture_sha256=hashlib.sha256(test.read_bytes()).hexdigest(),boundary='Synthetic pixel/constant and debug-layer validation on WARP/hardware FL11.0/11.1. Not game acceptance.')
(out/'compute-test.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(s.stdout,s.stderr);assert s.returncode==0,s.returncode
