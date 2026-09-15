"""Offline queue contract fixture. Does not attach to or launch a game."""
import argparse,subprocess,json,hashlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-root',required=True);p.add_argument('--output',required=True);p.add_argument('--editor-merge',action='store_true',help='Verify scale-only applied/editor merge (V32+)');a=p.parse_args()
root=Path(a.source_root).resolve();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
env={}
for line in subprocess.check_output(['cmd','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True).splitlines():
 k,sep,v=line.partition('=')
 if k and sep:env[k.upper()]=v
src=Path(__file__).parent/'tests/native-scale-queue.cpp';cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
cmd=[str(cl),'/nologo','/W4','/WX','/EHsc','/std:c++17','/I'+str(root),str(src.resolve()),'/Fe:'+str(out/'native-scale-queue.exe'),'/Fo:'+str(out/'native-scale-queue.obj'),'/link','user32.lib']
if a.editor_merge:cmd.insert(1,'/DD18_TEST_EDITOR_MERGE')
build=subprocess.run(cmd,env=env,capture_output=True,text=True);(out/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8');build.check_returncode()
run=subprocess.run([str(out/'native-scale-queue.exe'),str(out/'D18NativeScale.jsonl')],capture_output=True,text=True,timeout=15);(out/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');run.check_returncode()
files=[src]+[root/'dlssnr'/n for n in ['NativeScaleRequest.h','WildlandsScaleQueue.h','WildlandsScaleProfile.h']]
(out/'validation.json').write_text(json.dumps(dict(passed=True,editor_merge_tested=a.editor_merge,live_game_tested=False,boundary='Synthetic native queue scheduling and lifecycle, not proof of game callback coverage or live SR switching',files=[dict(path=str(f),sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in files]),indent=2),encoding='utf-8')
print(run.stdout)
