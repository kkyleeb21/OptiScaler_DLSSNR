import pathlib
import struct
import tempfile
import unittest
import importlib.util

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("diag", HERE / "summarize-diagnostics.py")
diag = importlib.util.module_from_spec(spec); spec.loader.exec_module(diag)


def text(value, size):
    raw = value.encode()[:size - 1]
    return raw + b"\0" * (size - len(raw))


class DiagnosticsTests(unittest.TestCase):
    def make_ring(self, records, capacity=4, dropped=0):
        header = diag.HEADER.pack(b"D18DIAG", 1, diag.HEADER.size, diag.RECORD.size, capacity,
                                  42, len(records), dropped, 5000, text("DX12", 16), text("DX12", 16), text("game.exe", 64))
        slots = [bytes(diag.RECORD.size) for _ in range(capacity)]
        for r in records:
            values = [r.get("sequence", 1), r.get("ms", 1), r.get("frame", 0), 0,
                      r.get("queue", 0), 0, r.get("target", 0), r.get("completed", 0)]
            values += [0] * 6 + [r.get("result", 0), r.get("flags", 0)] + [1.0] * 5
            slots[(values[0] - 1) % capacity] = diag.RECORD.pack(*values, text(r["type"], 32), text(r.get("reason", ""), 96), 1)
        tmp = tempfile.NamedTemporaryFile(delete=False); tmp.write(header + b"".join(slots)); tmp.close()
        self.addCleanup(pathlib.Path(tmp.name).unlink)
        return pathlib.Path(tmp.name)

    def test_first_skip_and_counts(self):
        path = self.make_ring([{"sequence": 1, "type": "frame_contract", "queue": 7},
                               {"sequence": 2, "type": "nr_skip", "reason": "expired owner", "queue": 7},
                               {"sequence": 3, "type": "nr_skip", "reason": "expired owner", "queue": 7}])
        h, records = diag.read_ring(path); result = diag.summarize(h, records)
        self.assertEqual(result["first_anomaly"]["sequence"], 2)
        self.assertEqual(result["skip_reason_counts"]["expired owner"], 2)

    def test_overwrite_order_and_missing_evidence(self):
        path = self.make_ring([{"sequence": 5, "type": "frame_contract"},
                               {"sequence": 6, "type": "evaluate", "result": 1}], capacity=2, dropped=4)
        h, records = diag.read_ring(path); result = diag.summarize(h, records)
        self.assertEqual([r["sequence"] for r in records], [5, 6])
        self.assertEqual(h["dropped"], 4)
        self.assertEqual(len(result["evidence_gaps"]), 2)

    def test_old_owner_expiry_chain_is_identified_offline(self):
        path = self.make_ring([{"sequence": 1, "type": "frame_contract", "queue": 99, "ms": 1000},
                               {"sequence": 2, "type": "nr_skip", "reason": "waiting for an observed native SR submission queue", "queue": 99, "ms": 2601}])
        h, records = diag.read_ring(path); result = diag.summarize(h, records)
        self.assertIn("observed native SR", result["first_anomaly"]["reason"])

    def test_fence_gap_requires_submitted_flag(self):
        path = self.make_ring([
            {"sequence": 1, "type": "evaluate", "queue": 7, "target": 11, "completed": 10},
            {"sequence": 2, "type": "composed", "queue": 7, "target": 12, "completed": 11,
             "flags": diag.FLAG_SUBMISSION_SUBMITTED},
            {"sequence": 3, "type": "composed", "queue": 7, "target": 13, "completed": 12,
             "flags": diag.FLAG_SUBMISSION_SUBMITTED | diag.FLAG_SUBMISSION_COMPLETE},
        ])
        h, records = diag.read_ring(path); result = diag.summarize(h, records)
        self.assertEqual([item["sequence"] for item in result["pending_recordings"]], [1])
        self.assertEqual([item["sequence"] for item in result["incomplete_fences"]], [2])

    def test_ui_observation_summary_never_claims_gameplay(self):
        header = {"game": "fixture", "backend": "fixture", "session": 1, "dropped": 0}
        common = {"type": "ui_scroll", "reason": "D3D12/poll/queue", "fence_target": 0,
                  "white_point": 0, "ratio": 0, "flags": 0}
        records = [dict(common, sequence=1, flags=1|2|4|32|64, ratio=10, white_point=-1),
                   dict(common, sequence=2, flags=1|2|32, ratio=200)]
        result = diag.summarize(header, records)
        ui = result["ui_input"]
        self.assertEqual(ui["observed_scroll_range"], [10, 200])
        self.assertEqual(ui["scrollbar_active_samples"], 1)
        self.assertEqual(ui["button_mismatch_samples"], 1)
        self.assertEqual(ui["gameplay_verdict"], "not_inferred")
        self.assertIn("Gameplay success is not inferred", diag.markdown(result))

    def test_rejects_truncated_schema(self):
        tmp = tempfile.NamedTemporaryFile(delete=False); tmp.write(b"D18"); tmp.close()
        self.addCleanup(pathlib.Path(tmp.name).unlink)
        with self.assertRaises(ValueError): diag.read_ring(pathlib.Path(tmp.name))


if __name__ == "__main__": unittest.main()
