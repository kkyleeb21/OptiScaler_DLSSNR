"""Prepare an explicit owned-host intervention successor; never modify a game.

The template is a diagnostic-only derivative of the read-only observer. Its two
policies isolate per-pass counter age from a one-time second-pass cold start.
"""
from pathlib import Path
import argparse,hashlib,json,shutil
p=argparse.ArgumentParser();p.add_argument('--baseline',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
assert not a.output.exists()
template=Path(__file__).with_name('nr-counter-bank-debugger.cpp')
assert template.is_file() and not (a.baseline/'core-source/tools/D18'/template.name).exists()
a.output.mkdir(parents=True)
shutil.copytree(a.baseline/'core-source',a.output/'core-source')
shutil.copy2(a.baseline/'run-multipass-checks.py',a.output/'run-multipass-checks.py')
(a.output/'tests').mkdir()
shutil.copy2(a.baseline/'tests/probe-reset-boundary-runtime.exe',a.output/'tests/probe-reset-boundary-runtime.exe')
shutil.copy2(template,a.output/'core-source/tools/D18'/template.name)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
(a.output/'preparation.json').write_text(json.dumps(dict(schema='d18-counter-isolation-preparation-v2',baseline=str(a.baseline.resolve()),scope='complete successor; add explicit owned-host counter intervention only; no game or runtime file changes',source_sha256=sha(template),host_sha256=sha(a.output/'tests/probe-reset-boundary-runtime.exe')),indent=2),encoding='utf-8')
print(a.output)
