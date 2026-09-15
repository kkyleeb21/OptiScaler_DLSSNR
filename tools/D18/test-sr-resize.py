"""Run the retained in-process resize/GPU-drain regression against an exact candidate."""
import argparse,hashlib,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',required=True)
a=p.parse_args();r=Path(a.candidate).resolve();fixture=Path(__file__).parent/'tests/wildlands-resize-host.cpp'
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if digest(fixture)!=digest(r/'sr-host.cpp'):raise SystemExit('Resize fixture identity differs; pin the intended candidate first')
subprocess.run([sys.executable,'-X','utf8',str(r/'test-sr.py'),'--resize','offscreen-handoff'],check=True)
