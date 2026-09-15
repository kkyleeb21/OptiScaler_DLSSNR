"""Explicit bounded NR resource benchmark; no game discovery, writes, or clock changes."""
from pathlib import Path
import argparse,csv,datetime,hashlib,json,re,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--forwarder',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--driver',type=Path,required=True)
p.add_argument('--mode',choices=['independent_batch','shared_batch'],required=True);p.add_argument('--size',choices=['1080p','4k'],required=True);p.add_argument('--ratio',type=float,choices=[.5,1.],default=.5);p.add_argument('--run',required=True);a=p.parse_args()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
assert a.run.replace('_','').isalnum()
expected={'forwarder':'D187321BCD70F3C3A0B1BB18E4178CAC0C6A8AAA585C05352EDD2DA8A22D722E','runtime':'CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414'}
for name,value in expected.items():assert sha(getattr(a,name))==value,'Re-audit runtime identity before use'
root=a.build.resolve();out=root/'tests'/a.run;assert not out.exists();out.mkdir(parents=True)
private=root/'private-runtime';private.mkdir(exist_ok=True)
for name,file in [('forwarder','nvngx.dll_dlssnr.dll'),('runtime','nvngx_dlssnr.dll')]:
 dst=private/file
 if not dst.exists():shutil.copy2(getattr(a,name),dst)
 assert sha(dst)==expected[name]
data=root/'runtime-data';data.mkdir(exist_ok=True)
exe=root/'tests/probe-nr-resources.exe';w,h=(1920,1080) if a.size=='1080p' else (3840,2160)
m=dict(schema='d18-nr-resource-probe-v1',start=datetime.datetime.now().astimezone().isoformat(),mode=a.mode,width=w,height=h,ratio=a.ratio,style=2,passes=2,created_features=2 if a.mode=='independent_batch' else 1,frames=48,warmup=16,measured_frames=list(range(16,48)),complete=False,
       executable_sha256=sha(exe),runtime_sha256=expected,driver_sha256=sha(a.driver),cross_frame_gpu_wait=True,pixel_readback='initial input and final outputs only; finite statistics, no pixel files',gpu_timing_scope='Each Evaluate command interval; excludes SR/FG/prepare/compose/readback; same queue timestamps',power_state_changed=False)
try:
 with (out/'probe.log').open('w',encoding='utf-8') as log:
  result=subprocess.run([str(exe),str(private/'nvngx.dll_dlssnr.dll'),str(private/'nvngx_dlssnr.dll'),str(data),str(a.driver.resolve()),a.mode,str(out),'2',str(w),str(h),str(a.ratio)],cwd=out,stdout=log,stderr=subprocess.STDOUT,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
 m['exit_code']=result.returncode
 log=(out/'probe.log').read_text();rows=list(csv.DictReader((out/'timings.csv').open())) if (out/'timings.csv').exists() else []
 m['debug_layer_enabled']='debug_layer=1' in log
 m['memory_samples']=[dict(stage=s,local_usage=int(u),budget=int(b)) for s,u,b in re.findall(r'memory stage=(\w+) local_usage=(\d+) budget=(\d+)',log)]
 m['complete']=result.returncode==0 and len(rows)==48 and [int(r['frame']) for r in rows]==list(range(48)) and 'PASS NR resource probe' in log
 m['files']={f.name:sha(f) for f in [out/'probe.log',out/'timings.csv'] if f.exists()}
finally:
 m['end']=datetime.datetime.now().astimezone().isoformat();(out/'manifest.json').write_text(json.dumps(m,indent=2),encoding='utf-8')
print(json.dumps({k:m[k] for k in ['mode','width','height','complete','start','end']}));print('\n'.join((out/'probe.log').read_text().splitlines()[-4:]));raise SystemExit(0 if m['complete'] else 1)
