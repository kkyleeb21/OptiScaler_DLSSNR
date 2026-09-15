"""Run the complete native handoff and observed-resolution quality matrix."""
import argparse,hashlib,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--candidate',required=True)
a=p.parse_args();r=Path(a.candidate).resolve()
fixture=Path(__file__).parent/'tests/wildlands-handoff-host.cpp'
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if digest(fixture)!=digest(r/'sr-host.cpp'):raise SystemExit('Fixture identity differs; pin intended candidate first')
subprocess.run([sys.executable,'-X','utf8',str(r/'test-sr.py'),'offscreen-handoff','offscreen-compute','offscreen-100','offscreen-67','offscreen-58','offscreen-50','offscreen-33'],check=True)
