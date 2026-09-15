"""Run a retained native FG bring-up candidate. Does not claim gameplay acceptance."""
import argparse,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--candidate',type=Path,required=True);mode=p.add_mutually_exclusive_group();mode.add_argument('--timeout',action='store_true');mode.add_argument('--quiet',action='store_true',help='Verify Summary-off functionality and inert legacy research markers (candidate fixture support required)');p.add_argument('--start-off',action='store_true',help='Prepare FG with generation off, hot-enable before SR, then exercise on/off recovery');p.add_argument('--diagnostic',action='store_true');p.add_argument('--fl11-1',action='store_true',dest='fl11_1');a=p.parse_args()
r=a.candidate.resolve();root=Path('E:/DLSSNR/builds').resolve()
if not r.is_relative_to(root):raise SystemExit('Candidate must be under E:/DLSSNR/builds')
fixture=r/'test-fg.py'
flags=(['--timeout'] if a.timeout else ['--quiet'] if a.quiet else [])+(['--diagnostic'] if a.diagnostic else [])+(['--fl11-1'] if a.fl11_1 else [])+(['--start-off'] if a.start_off else [])
code=fixture.read_text(encoding='utf-8-sig')
for flag in flags:
 if repr(flag) not in code:raise SystemExit('Candidate fixture does not support '+flag+'; no test was run')
raise SystemExit(subprocess.call([sys.executable,str(fixture)]+flags,cwd=r))
