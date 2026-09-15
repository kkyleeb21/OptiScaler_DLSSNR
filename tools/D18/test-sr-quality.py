"""Build and run SR quality policy checks."""
import argparse
import json
import hashlib
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
source = Path(args.source_root).resolve()
out = Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=True)
env = {}
setup = subprocess.check_output(['cmd', '/c', r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'], text=True)
for line in setup.splitlines():
    key, separator, value = line.partition('=')
    if key and separator:
        env[key.upper()] = value
cl = Path(env['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
fixture = Path(__file__).parent / 'tests/sr-quality-host.cpp'
command = [str(cl), '/nologo', '/W4', '/WX', '/EHsc', '/std:c++17', '/I' + str(source),
           str(fixture.resolve()), '/Fe:' + str(out / 'host.exe'), '/Fo:' + str(out / 'host.obj')]
build = subprocess.run(command, env=env, capture_output=True, text=True)
(out / 'build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
if build.returncode:
    raise SystemExit('Build failed; see build.log')
run = subprocess.run([str(out / 'host.exe')], capture_output=True, text=True, timeout=60)
(out / 'host.stdout.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
run.check_returncode()
assert 'PASS: 15 quality policy cases' in run.stdout
(out / 'validation.json').write_text(json.dumps(dict(passed=True, cases=15, api='CPU policy', source_sha256=hashlib.sha256((source / 'dlssnr/SrQualityMode.h').read_bytes()).hexdigest(), boundary='Dimension classification only; no GPU or gameplay acceptance'), indent=2), encoding='utf-8')
print(run.stdout, end='')
print('PASS: SR quality mapping')

