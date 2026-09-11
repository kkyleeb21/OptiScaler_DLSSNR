"""Build/run isolated fixed-hash DX11 probe; no modification to runtime or games."""
import datetime,hashlib,json,pathlib,subprocess,sys,shutil
ROOT=pathlib.Path(__file__).resolve().parents[2]
src=ROOT/'workspace/dlss5/extracted/DLSS310.8.0-Streamline2.13/nvngx_dlssnr.dll'
expected='e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e'
assert hashlib.sha256(src.read_bytes()).hexdigest()==expected
out=ROOT/'evidence/D24'/('dx11-live-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
allowed={'--kernel','--nr-create','--nr-create-experiment','--nr-create-adapter','--nr-create-evaluate','--nr-create-dry'}
assert len(sys.argv)<=2 and all(x in allowed or (x.startswith('--nr-create-prefix') and x[18:].isdigit() and 1<=int(x[18:])<=156) for x in sys.argv[1:]),'Unknown or conflicting probe mode'
for name in ['dx11_backend_probe.cpp','dx11_probe_parameters.h','dx11_probe_kernel.cu','run_dx11_backend_probe.py']:
    shutil.copy2(ROOT/'tools/D24'/name,out/name)
code=out/'dx11_backend_probe.cpp'
batch=out/'build.cmd'
commands='@echo off\ncall "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /W4 /WX /EHsc /std:c++17 "'+str(code)+'" /Fe:"'+str(out/'probe.exe')+'" /Fo:"'+str(out/'probe.obj')+'"\nif errorlevel 1 exit /b 1\n'
commands=commands.replace('cl /nologo','cl /I"'+str(ROOT/'workspace/dlss5/worktrees/d18-012-re-integration/external/nvngx_dlss_sdk')+'" /nologo')
kernel='--kernel' in sys.argv
if kernel:commands+='nvcc --cubin -arch=sm_120 "'+str(ROOT/'tools/D24/dx11_probe_kernel.cu')+'" -o "'+str(out/'kernel.cubin')+'"\n'
batch.write_text(commands)
build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,capture_output=True,text=True)
(out/'build.txt').write_text(build.stdout+build.stderr)
assert build.returncode==0,build.stdout+build.stderr
try:
    nr=[x for x in sys.argv[1:] if x.startswith('--nr-create')]
    run=subprocess.run([str(out/'probe.exe'),str(src)]+([str(out/'kernel.cubin')] if kernel else nr),cwd=out,capture_output=True,text=True,timeout=45)
    (out/'events.jsonl').write_text(run.stdout)
    result=dict(returncode=run.returncode,stderr=run.stderr)
except subprocess.TimeoutExpired as e:
    (out/'events.jsonl').write_bytes(e.stdout or b'')
    result=dict(timeout=True)
result.update(source=str(src),sha256_before=expected,sha256_after=hashlib.sha256(src.read_bytes()).hexdigest(),probe_sha256=hashlib.sha256((out/'probe.exe').read_bytes()).hexdigest())
result['mode']=sys.argv[1:] or ['init-only']
result['source_hashes']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.suffix in ['.cpp','.h','.cu','.py']}
result['gpu']=subprocess.run(['nvidia-smi','--query-gpu=name,driver_version,utilization.gpu,memory.used','--format=csv,noheader'],capture_output=True,text=True,timeout=10).stdout.strip()
(out/'runtime.log').write_text(result.get('stderr',''),encoding='utf-8')
(out/'result.json').write_text(json.dumps(result,indent=2))
print(out);print((out/'events.jsonl').read_text());print(json.dumps(result,indent=2))
assert result['sha256_after']==expected
