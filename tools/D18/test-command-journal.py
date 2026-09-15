"""Build a CPU-only command journal pressure fixture; requires Visual C++ Build Tools."""
import argparse
import importlib.util
import json
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
fixture = Path(__file__).parent / 'tests/command-journal-host.cpp'
command = [str(cl), '/nologo', '/W4', '/WX', '/EHsc', '/std:c++17', '/DD18_DIAGNOSTIC_BUILD=1', '/I' + str(source),
           str(fixture.resolve()), '/Fe:' + str(out / 'host.exe'), '/Fo:' + str(out / 'host.obj')]
build = subprocess.run(command, env=env, capture_output=True, text=True)
(out / 'build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
if build.returncode:
    raise SystemExit('Build failed; see build.log')
subprocess.run([str(out / 'host.exe'), str(out)], check=True, timeout=60)
spec = importlib.util.spec_from_file_location('summary', Path(__file__).parent / 'summarize-command-lists.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
result = module.summarize(out / 'D18CommandLists.jsonl')
(out / 'summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
assert len(result['frames']) == 4 and not result['validation_issues']
for frame in result['frames']:
    pressure = frame['frame'] == 3
    assert frame['present_pair_observed'] and frame['same_backbuffer_identity'] is None
    assert {'ui_begin', 'ui_end', 'sr_private_seed'} <= {p['event'] for p in frame['phases']}
    assert len(frame['executions']) == (124 if pressure else 121)
    assert frame['envelope_complete'] == (not pressure)
    assert frame['selected_details_complete'] == (not pressure)
    assert frame['intentionally_omitted_lists'] == 120
(out / 'validation.json').write_text(json.dumps(dict(passed=True, records=result['records'],
    bytes=(out / 'D18CommandLists.jsonl').stat().st_size,
    boundary='CPU-only metadata pressure fixture, not GPU/game acceptance'), indent=2), encoding='utf-8')
print('PASS: unrelated-list pressure, complete critical details, overflow phase reserve, later-frame capture')
conflict = module.summarize(out / 'D18CommandConflicts.jsonl')
(out / 'conflict-summary.json').write_text(json.dumps(conflict, indent=2), encoding='utf-8')
assert not conflict['validation_issues'] and len(conflict['frames']) == 3
assert [f['frame'] for f in conflict['frames']] == [30, 31, 32]
assert [f['executions'][0]['trace_records'] for f in conflict['frames']] == [512, 1500, 1500]
assert [f['selected_details_complete'] for f in conflict['frames']] == [False, True, True]
assert all(f['end']['scope'] == 'single_conflicting_execute' and not f['present_pair_observed'] for f in conflict['frames'])
print('PASS: triggered short-list snapshots, first partial trace distinguished, three-snapshot budget, trace lease expiry')
