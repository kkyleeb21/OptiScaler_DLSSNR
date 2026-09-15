"""Prepare/test a bounded read-only setting observer; optionally observe a supplied GRW PID."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',required=True);p.add_argument('--pid',type=int);p.add_argument('--seconds',type=int,default=30)
p.add_argument('--profile',required=True,help='Pinned static PE inventory for this exact GRW executable')
p.add_argument('--render-state',action='store_true',help='Also read the render-settings scale and owning window thread once; does not establish Apply caller thread')
a=p.parse_args();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
if not 1<=a.seconds<=60:raise SystemExit('Capture limit must be 1..60 seconds')
env={}
for line in subprocess.check_output(['cmd','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True).splitlines():
 k,sep,v=line.partition('=')
 if k and sep:env[k.upper()]=v
cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
for name in ('native-settings-watch','native-settings-watch-fixture'):
 src=Path(__file__).parent/'tests'/f'{name}.cpp'
 run=subprocess.run([str(cl),'/nologo','/W4','/WX','/EHsc','/std:c++17',str(src.resolve()),'/Fe:'+str(out/f'{name}.exe'),'/Fo:'+str(out/f'{name}.obj')],env=env,capture_output=True,text=True)
 (out/f'{name}-build.log').write_text(run.stdout+run.stderr,encoding='utf-8');run.check_returncode()
subprocess.run([str(out/'native-settings-watch-fixture.exe'),str(out/'native-settings-watch.exe'),str(out/'fixture.jsonl')],check=True,timeout=10)
events=[json.loads(l) for l in (out/'fixture.jsonl').read_text().splitlines()]
for slot in (0,1):assert [e['scale_field'] for e in events if e['event']=='settings_snapshot' and e['slot']==slot]==[.75,.5]
assert events[-1]['event']=='settings_watch_end' and not events[-1]['at_limit']
probe=next(e for e in events if e['event']=='render_settings_probe')
assert probe['available'] and probe['render_scale_field']==.625 and probe['pending_field']==0 and probe['window_owned_by_process'] and probe['window_thread']>0
(out/'validation.json').write_text(json.dumps(dict(passed=True,boundary='Synthetic process transitions only; no real game apply-call identity established',observer_sha256=hashlib.sha256((out/'native-settings-watch.exe').read_bytes()).hexdigest()),indent=2),encoding='utf-8')
print('PASS: read-only observer sees synthetic pending/applied transitions')
if a.pid:
 profile=json.loads(Path(a.profile).read_text(encoding='utf-8-sig'));exe=Path(profile['image'])
 if exe.name.lower()!='grw.exe' or hashlib.sha256(exe.read_bytes()).hexdigest()!=profile['sha256']:raise SystemExit('Game image does not match pinned profile')
 subprocess.run([str(out/'native-settings-watch.exe'),str(a.pid),str(a.seconds),str(exe),str(out/'game-settings.jsonl')]+(['--render-state'] if a.render_state else []),check=True,timeout=a.seconds+10)
 print('Captured game settings; snapshots do not establish the native setter thread or GPU correctness')
