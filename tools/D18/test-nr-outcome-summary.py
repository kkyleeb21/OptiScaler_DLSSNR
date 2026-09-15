"""Round-trip production CPU outcome events through the unchanged ring schema and shared parser."""
import importlib.util
import pathlib
import struct
import subprocess
import sys
import tempfile

here = pathlib.Path(__file__).parent
spec = importlib.util.spec_from_file_location("summary", here / "summarize-diagnostics.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
output = subprocess.check_output([sys.argv[1]], text=True)
print(output, end="")
records = []
for line in output.splitlines():
    if not line.startswith("EVENT|"): continue
    _, frame, flags, width, height, result, reason = line.split("|", 6)
    seq = len(records) + 1
    records.append(m.RECORD.pack(seq, 1000, int(frame), 0, 0, 0, 0, 0,
        int(width), int(height), 0, 0, 0, 0, int(result), int(flags),
        1, 1, 1, 0, 0, b"nr_outcome", reason.encode(), 1))
assert len(records) == 3
header = m.HEADER.pack(b"D18DIAG", 1, m.HEADER.size, m.RECORD.size, 3, 1, 3, 0, 0,
                      b"observed", b"DX12", b"cpu-test")
with tempfile.TemporaryDirectory() as directory:
    ring = pathlib.Path(directory) / "test.ring"
    ring.write_bytes(header + b"".join(records))
    h, decoded = m.read_ring(ring)
    s = m.summarize(h, decoded)
    counts = s["nr_outcome_summary"]
    assert counts["observed_attempts"] == 601
    assert counts["sr_fallback_attempts"] == 300
    assert counts["reduced_pass_attempts"] == 1
    assert counts["requested_to_composed"] == {"2->2": 300, "2->0": 300, "4->2": 1}
    assert counts["reset_reasons"] == {"recording_gap": 1}
    assert s["nr_outcomes"][2]["reset_mask"] == 3
    assert all(o["source_id"] == 10 for o in s["nr_outcomes"])
    assert not s["incomplete_fences"] and not s["high_resolution_contracts"]
    assert "Observed attempts: 601" in m.markdown(s)
    # Turning NR off is an observation, not a reported runtime failure.
    off = dict(decoded[0], type="nr_skip", reason="it is switched off", result=0)
    old = m.summarize(h, [off])
    assert old["first_anomaly"] is None and old["nr_outcome_summary"]["observed_attempts"] == 0
    # No outcomes in a legacy C1 recording is explicitly unobserved.
    assert "unobserved" in old["nr_outcome_summary"]["coverage"]
print("PASS production outcome -> schema-1 binary -> shared JSON/Markdown, legacy/off semantics; CPU only")
