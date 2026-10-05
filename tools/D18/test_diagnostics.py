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
    def test_exposure_channels_distinguish_missing_zero_false_and_sample_frame(self):
        path = self.make_ring([
            dict(sequence=1, type='exposure_probe', frame=200, flags=1|2|4|16|256|4096),
            dict(sequence=2, type='exposure_probe', frame=201, flags=128)])
        data=bytearray(path.read_bytes())
        v=list(diag.RECORD.unpack_from(data,diag.HEADER.size))
        v[4]=180; v[12]=64; v[13]=256|128; v[16]=0.0; v[17]=0.0; v[20]=-8.0
        diag.RECORD.pack_into(data,diag.HEADER.size,*v);path.write_bytes(data)
        h, records=diag.read_ring(path);result=diag.summarize(h,records)
        a,b=result['exposure_channels']
        self.assertEqual(a['pre_exposure'],0.0);self.assertEqual(a['exposure_scale'],0.0)
        self.assertIs(a['texture_offered'],False);self.assertIs(a['auto_exposure'],False)
        self.assertIsNone(a['texture_readback']);self.assertEqual(a['driver'],'SR')
        self.assertEqual(a['luma']['frame'],180);self.assertTrue(a['luma']['nr_enabled'])
        self.assertEqual(a['luma']['mean_log2'],-8.0)
        self.assertIsNone(b['pre_exposure']);self.assertIsNone(b['texture_offered'])
        self.assertIsNone(b['driver']);self.assertIsNone(b['luma']['mean_log2'])
        self.assertIsNone(a['streamline_options']['preExposure'])
        self.assertIn('Pre_Exposure=0.0',diag.markdown(result))
        self.assertIn('AutoExposure=false',diag.markdown(result))
        self.assertIsNone(result['first_anomaly'])

    def test_estimate_whitepoint_source_lraw_b_and_hold(self):
        bits=struct.unpack('<I',struct.pack('<f',-7.3))[0]
        h, records=diag.read_ring(self.make_ring([dict(type='exposure_probe', flags=131072|262144|1048576|4096, result=bits)]))
        records[0].update(white_point=.004, mv_scale_y=-12.3, guide_width=4096)
        summary=diag.summarize(h,records);e=summary['exposure_channels'][0]
        self.assertEqual(e['white_source'],'hold')
        self.assertAlmostEqual(e['b'],-7.3,places=5)
        self.assertEqual(e['Lraw'],-12.3);self.assertEqual(e['W'],.004)
        self.assertIn('16_stratified',e['luma']['method'])
        self.assertIn('Lraw=-12.3',diag.markdown(summary))
        self.assertIsNone(summary['first_anomaly'])

    def test_hook_lifecycle_error_preserves_code(self):
        header, records = diag.read_ring(self.make_ring([
            dict(type="dlssg_hook", reason="detach/modify", result=487)]))
        result = diag.summarize(header, records)
        self.assertEqual(result["dlssg_hooks"][0]["result"], 487)
        self.assertIn("detach/modify: code 0x1E7", diag.markdown(result))

    def test_output_rect_dimensions_are_not_network_dimensions(self):
        path = self.make_ring([dict(type="sr_output_rect", reason="yysls_render_alias_expanded")])
        header, records = diag.read_ring(path)
        records[0].update(width=3840, height=2160, network_width=1920,
                          network_height=1080, guide_width=1920, guide_height=1080)
        result = diag.summarize(header, records)
        rect = result["sr_output_rects"][0]
        self.assertEqual(rect["effective_output"], [3840, 2160])
        self.assertEqual(rect["reported_output"], [1920, 1080])
        self.assertIsNone(result["first_anomaly"])
        self.assertIn("yysls_render_alias_expanded", diag.markdown(result))

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

    def test_rejects_truncated_schema(self):
        tmp = tempfile.NamedTemporaryFile(delete=False); tmp.write(b"D18"); tmp.close()
        self.addCleanup(pathlib.Path(tmp.name).unlink)
        with self.assertRaises(ValueError): diag.read_ring(pathlib.Path(tmp.name))


if __name__ == "__main__": unittest.main()
