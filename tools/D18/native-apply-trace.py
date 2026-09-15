"""Build and test a bounded one-shot native Apply thread/stack observer.
Uses a hardware execution breakpoint and a debugger attachment, not ordinary
read-only polling. Never invokes Apply or changes engine settings.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',required=True)
p.add_argument('--profile',help='PE inventory with a unique primary unwind entry')
p.add_argument('--pid',type=int)
p.add_argument('--rva',type=lambda s:int(s,0),default=0x1372170)
p.add_argument('--seconds',type=int,default=60)
a=p.parse_args();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
if a.pid:raise SystemExit('Live attachment disabled: Wildlands exited before user input during the first live trial. This tool is fixture-only pending review.')
if not 1<=a.seconds<=60:raise SystemExit('Limit must be 1..60 seconds')
(out/'validation.json').write_text(json.dumps(dict(passed=False,state='validation_in_progress')),encoding='utf-8')
env={}
for line in subprocess.check_output(['cmd','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True).splitlines():
 k,sep,v=line.partition('=')
 if k and sep:env[k.upper()]=v
cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
for name in ('native-apply-trace','native-apply-trace-fixture'):
 src=Path(__file__).parent/'tests'/f'{name}.cpp'
 args=[str(cl),'/nologo','/W4','/WX','/EHsc','/std:c++17',str(src.resolve()),'/Fe:'+str(out/f'{name}.exe'),'/Fo:'+str(out/f'{name}.obj')]
 if name=='native-apply-trace':args+=['/link','dbgeng.lib']
 run=subprocess.run(args,env=env,capture_output=True,text=True)
 (out/f'{name}-build.log').write_text(run.stdout+run.stderr,encoding='utf-8')
 run.check_returncode()
for mode in ('hit','timeout'):
 run=subprocess.run([str(out/'native-apply-trace-fixture.exe'),str(out/'native-apply-trace.exe'),str(out/f'fixture-{mode}.jsonl'),mode],capture_output=True,text=True,timeout=30)
 (out/f'fixture-{mode}.txt').write_text(run.stdout+run.stderr,encoding='utf-8');run.check_returncode()
 rows=[json.loads(x) for x in (out/f'fixture-{mode}.jsonl').read_text().splitlines()]
 assert rows[-1]['event']=='trace_end' and rows[-1]['detach_hresult']==0 and rows[-1]['hit']==(mode=='hit')
 if mode=='hit':
  hit=next(x for x in rows if x['event']=='native_apply_hit')
  assert hit['thread_id'] and hit['stack'] and hit['rcx_available']
validation=dict(passed=True,live_enabled=False,allowed_target='synthetic fixture only',boundary='Synthetic debugger attach/hardware hit/timeout/detach only; first game trial failed before user operation',sha256=hashlib.sha256((out/'native-apply-trace.exe').read_bytes()).hexdigest())
(out/'validation.json').write_text(json.dumps(validation,indent=2),encoding='utf-8')
print('PASS: one-shot hardware capture and timeout both detach; fixture keeps running')
