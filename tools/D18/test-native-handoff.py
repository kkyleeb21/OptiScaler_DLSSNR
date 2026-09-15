"""Run the retained native-colour handoff fixture against a complete candidate."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--candidate', required=True)
args = parser.parse_args()
candidate = Path(args.candidate).resolve()
fixture = Path(__file__).parent / 'tests/wildlands-handoff-host.cpp'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
if digest(fixture) != digest(candidate / 'sr-host.cpp'):
    raise SystemExit('Fixture differs from shared version; review and pin the intended candidate first')
subprocess.run([sys.executable, '-X', 'utf8', str(candidate / 'test-sr.py'),
                'offscreen-handoff', 'offscreen-compute'], check=True)
