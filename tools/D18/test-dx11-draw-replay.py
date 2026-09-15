from pathlib import Path
import subprocess,sys,json
r=Path(sys.argv[1]).resolve();out=r/'draw-replay-test';out.mkdir(exist_ok=True);env={}
for line in subprocess.check_output(['cmd','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True).splitlines():
 k,sep,v=line.partition('=')
 if k and sep:env[k.upper()]=v
cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
args=[str(cl),'/nologo','/W4','/WX','/EHsc','/std:c++17','/I'+str(r/'core-source/OptiScaler/dlssnr'),str(Path(__file__).with_name('dx11-draw-replay-host.cpp')),'/Fe:'+str(out/'host.exe'),'/Fo:'+str(out/'host.obj')]
build=subprocess.run(args,env=env,capture_output=True,text=True);(out/'build.log').write_text(build.stdout+build.stderr);assert build.returncode==0,build.stdout+build.stderr
run=subprocess.run([str(out/'host.exe')],capture_output=True,text=True,timeout=60)
(out/'results.json').write_text(json.dumps(dict(exit=run.returncode,stdout=run.stdout,stderr=run.stderr),indent=2));print(run.stdout);assert run.returncode==0
