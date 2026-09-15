"""Check evidence integrity against a real command journal plus damaged variants."""
import argparse
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('command_summary', Path(__file__).parents[1] / 'summarize-command-lists.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
parser = argparse.ArgumentParser()
parser.add_argument('journal')
parser.add_argument('--work-dir', required=True)
args = parser.parse_args()
raw = Path(args.journal).read_bytes()
baseline = module.summarize(args.journal)
assert baseline['frames'] and not baseline['validation_issues']
assert all(f['envelope_complete'] and f['execution_details_complete'] and f['present_pair_observed'] for f in baseline['frames'])
directory = Path(args.work_dir).resolve()
directory.mkdir(parents=True, exist_ok=True)
path = directory / 'sample.jsonl'
path.write_bytes(raw + b'{"event":')
assert module.summarize(path)['partial_tail']
path.write_bytes(raw.split(b'\n', 1)[0] + b'\nINVALID\n' + raw.split(b'\n', 1)[1])
try:
    module.summarize(path)
    raise AssertionError('Corrupted middle record was accepted')
except ValueError:
    pass
items = [json.loads(line) for line in raw.splitlines()]
execute = next(i for i in items if i['event'] == 'execute')
execute['decision'], execute['reasons'] = 0, 4
path.write_text(''.join(json.dumps(i) + '\n' for i in items), encoding='utf-8')
assert module.summarize(path)['validation_issues'][0]['issue'] == 'disjoint_with_incomplete_metadata'
execute['decision'], execute['reasons'], execute['trace_dropped'] = 1, 4, 3
path.write_text(''.join(json.dumps(i) + '\n' for i in items), encoding='utf-8')
result = module.summarize(path)
assert not result['frames'][0]['execution_details_complete']
assert not result['frames'][0]['write_metadata_complete']
# A missing end marker cannot become a complete frame.
path.write_bytes(b'\n'.join(raw.splitlines()[:-1]) + b'\n')
assert not module.summarize(path)['frames'][-1]['envelope_complete']
print('PASS: real frames, partial tail, middle corruption, unsafe admission, trace loss, missing frame end')
