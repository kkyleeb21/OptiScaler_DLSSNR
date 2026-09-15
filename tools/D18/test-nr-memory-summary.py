import importlib.util
import pathlib
import subprocess
import sys

here = pathlib.Path(__file__).parent
spec = importlib.util.spec_from_file_location('summary', here / 'summarize-diagnostics.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
output = subprocess.check_output([sys.argv[1]], text=True)
print(output, end='')
records = []
for line in output.splitlines():
    if not line.startswith('MEM|'): continue
    _, generation, luid, device, flags, result, reason = line.split('|', 6)
    records.append(m.RECORD.pack(len(records)+1, 1000, 1, int(generation), int(luid), int(device), 0, 0,
        3840, 2160, 1920, 1080, 0, 0, int(result), int(flags), .5, 1, 1, 0, 0,
        b'nr_memory', reason.encode(), 1))
header = m.HEADER.pack(b'D18DIAG', 1, m.HEADER.size, m.RECORD.size, len(records), 1, len(records), 0, 0,
                      b'observed', b'DX12', b'cpu-memory-test')
# Explicit fixture output directory avoids platform-specific tempfile ACL defaults.
ring = pathlib.Path(sys.argv[2]) / 'nr-memory-test.ring'
ring.parent.mkdir(parents=True, exist_ok=True)
ring.write_bytes(header + b''.join(records))
h, decoded = m.read_ring(ring)
s = m.summarize(h, decoded)
assert len(s['nr_memory']) == 4
assert s['nr_memory'][0]['budget_bytes'] == 10000
assert s['nr_memory'][0]['admitted'] is True
assert s['nr_memory'][0]['pass'] == 2
assert s['nr_memory'][0]['adapter_luid'] == 10
assert s['nr_memory'][1]['admitted'] is None
assert s['nr_memory'][3]['usage_bytes'] is None
assert [d['usage_delta_bytes'] for d in s['nr_memory_deltas']] == [400, 300]
assert not s['pending_recordings'] and not s['incomplete_fences']
assert s['first_anomaly'] is None
assert '400 bytes' in m.markdown(s)
# Different device/generation cannot be paired; negative process deltas are retained.
cross = [decoded[0], dict(decoded[1], command_list=99), dict(decoded[2], feature_generation=8)]
assert not m.summarize(h, cross)['nr_memory_deltas']
negative = dict(decoded[1], reason='created;b=2710;u=1;p=0;r=0')
assert m.summarize(h, [decoded[0], negative])['nr_memory_deltas'][0]['usage_delta_bytes'] == -999
assert not m.summarize(h, [])['nr_memory']
overflow = dict(decoded[0], reason='overflow;n=4', flags=0)
assert m.summarize(h, [overflow])['nr_memory_suppressed'] == 4
print('PASS production encoder -> schema-1 ring -> shared memory summary; missing/invalid/cross-generation/negative/overflow cases')
