"""Run the retained candidate's real-GPU 1:1 handoff regression without a game."""
import argparse,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',required=True);a=p.parse_args()
r=Path(a.candidate).resolve()
for name in ['test-sr.py','prepare-fixture.py','sr-host.cpp','core-output/OptiScaler.dll']:
 if not (r/name).is_file():raise SystemExit('Missing candidate artifact: '+name)
subprocess.run([sys.executable,'-X','utf8',str(r/'prepare-fixture.py')],check=True)
subprocess.run([sys.executable,'-X','utf8',str(r/'test-sr.py'),'--same-size','offscreen-handoff'],check=True)
