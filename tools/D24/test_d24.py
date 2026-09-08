import json
import pathlib
import struct
import subprocess
import sys
import contextlib
import uuid
import unittest

import ngx_audit as audit
import validate_probe as probe
from query_callpath import find_path

sys.path.insert(0, str(audit.ROOT / 'workspace/dlss5/tools/third_party/capstone'))


@contextlib.contextmanager
def evidence_directory():
    # Preserve tiny fixtures for inspection; no Temp ACL dependency or recursive cleanup.
    root = audit.EVIDENCE / 'test-fixtures' / uuid.uuid4().hex
    root.mkdir(parents=True)
    yield root


def fixture():
    b = bytearray(0xa00)
    def pack(fmt, off, *v):
        struct.pack_into(fmt, b, off, *v)
    b[:2] = b'MZ'
    pack('<I', 0x3c, 0x80)
    b[0x80:0x84] = b'PE\0\0'
    pack('<HH', 0x84, 0x8664, 1)
    pack('<H', 0x94, 240)
    opt = 0x98
    pack('<H', opt, 0x20b)
    pack('<Q', opt + 24, 0x180000000)
    pack('<I', opt + 60, 0x200)
    pack('<I', opt + 108, 16)
    pack('<II', opt + 112, 0x1000, 0x100)
    section = opt + 240
    b[section:section + 5] = b'.text'
    pack('<IIII', section + 8, 0x1000, 0x1000, 0x800, 0x200)
    pack('<I', section + 36, 0x60000020)
    # Two named exports, one ordinal-only, one EAT hole.
    pack('<IIIIII', 0x210, 5, 4, 2, 0x1040, 0x1050, 0x1058)
    pack('<IIII', 0x240, 0x1200, 0x1080, 0x1210, 0)
    pack('<II', 0x250, 0x1090, 0x10c0)
    pack('<HH', 0x258, 0, 1)
    for off, text in [(0x280, b'other.Forward\0'), (0x290, b'NVSDK_NGX_D3D11_Init\0'),
                      (0x2c0, b'NVSDK_NGX_D3D12_Init\0')]:
        b[off:off + len(text)] = text
    b[0x400:0x406] = bytes.fromhex('b8 01 00 00 00 c3')
    return b


class PEChecks(unittest.TestCase):
    def test_wrong_baseline_hash_refused_before_output(self):
        with evidence_directory() as root:
            dll = root / 'fixture.dll'
            dll.write_bytes(fixture())
            output = root / 'must-not-exist'
            run = subprocess.run([sys.executable, str(audit.ROOT / 'tools/D24/ngx_audit.py'),
                                  str(dll), '--output', str(output), '--expected-sha256', '0' * 64],
                                 capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn('baseline SHA256 mismatch', run.stderr)
            self.assertFalse(output.exists())

    def test_exports_forwarders_ordinals_and_holes(self):
        rows = audit.PE(fixture()).exports()
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[1]['forwarder'], 'other.Forward')
        self.assertEqual(rows[2]['names'], [])
        self.assertEqual(rows[2]['ordinal'], 7)

    def test_unbacked_virtual_tail_refused(self):
        with self.assertRaises(ValueError):
            audit.PE(fixture()).offset(0x1900)

    def test_truncated_pe(self):
        with self.assertRaises(ValueError):
            audit.PE(fixture()[:0x180])

    def test_invalid_named_ordinal(self):
        b = fixture()
        struct.pack_into('<H', b, 0x258, 10)
        with self.assertRaises(ValueError):
            audit.PE(b).exports()

    def test_bad_directory_count(self):
        b = fixture()
        struct.pack_into('<I', b, 0x98 + 108, 17)
        with self.assertRaises(ValueError):
            audit.PE(b)

    def test_normal_and_delay_imports(self):
        for delay in (False, True):
            b = fixture()
            struct.pack_into('<II', b, 0x98 + 112 + (13 if delay else 1) * 8, 0x1300, 64 if delay else 40)
            if delay:
                struct.pack_into('<8I', b, 0x500, 1, 0x1380, 0, 0x13c0, 0x13a0, 0, 0, 0)
            else:
                struct.pack_into('<5I', b, 0x500, 0x13a0, 0, 0, 0x1380, 0x13c0)
            b[0x580:0x58a] = b'nvcuda.dll'
            struct.pack_into('<3Q', b, 0x5a0, 0x13e0, (1 << 63) | 9, 0)
            b[0x5e2:0x5e9] = b'cuInit\0'
            rows = audit.PE(b).imports(delay)
            self.assertEqual(rows[0]['symbol'], 'cuInit')
            self.assertEqual(rows[1]['symbol'], 'ordinal:9')
            self.assertEqual(rows[0]['delay'], delay)

    def test_constant_body_does_not_certify_backend(self):
        pe = audit.PE(fixture())
        graph = audit.analyze(pe, pe.exports(), [], [])
        root = graph['nodes'][0]
        self.assertTrue(root['observation'].startswith('CONSTANT_RETURN_BODY'))
        self.assertEqual(len(root['instructions']), 2)

    def test_indirect_call_stays_unresolved(self):
        b = fixture()
        b[0x400:0x403] = bytes.fromhex('ff d0 c3')
        pe = audit.PE(b)
        root = audit.analyze(pe, pe.exports(), [], [])['nodes'][0]
        self.assertTrue(any('indirect call' in s for s in root['unresolved']))
        self.assertEqual(root['observation'], 'UNRESOLVED_BACKEND')

    def test_nondefault_image_base(self):
        b = fixture()
        struct.pack_into('<Q', b, 0x98 + 24, 0x140000000)
        b[0x400:0x406] = bytes.fromhex('e8 0b 00 00 00 c3')
        b[0x410] = 0xc3
        pe = audit.PE(b)
        root = audit.analyze(pe, pe.exports(), [], [])['nodes'][0]
        self.assertEqual(root['edges'][0]['target'], 0x1210)


class PixelChecks(unittest.TestCase):
    def test_opaque_black_does_not_count_as_nonzero(self):
        stats, _ = probe.pixels(bytes([0, 0, 0, 255]), 1, 1, 'RGBA8_UNORM', 4)
        self.assertEqual(stats['nonzero_rgb_fraction'], 0)

    def test_padding_excluded_from_hash(self):
        a, _ = probe.pixels(bytes([1, 2, 3, 255, 9, 9, 9, 9]), 1, 1, 'RGBA8_UNORM', 8)
        b, _ = probe.pixels(bytes([1, 2, 3, 255]), 1, 1, 'RGBA8_UNORM', 4)
        self.assertEqual(a['sha256'], b['sha256'])

    def test_half_nan_detected(self):
        s, _ = probe.pixels(struct.pack('<4e', float('nan'), 0, 0, 1), 1, 1, 'RGBA16_FLOAT', 8)
        self.assertEqual(s['finite_fraction'], 0)

    def test_wrong_size_rejected(self):
        with self.assertRaises(ValueError):
            probe.pixels(b'\0', 1, 1, 'RGBA8_UNORM', 4)

    def test_path_escape_rejected(self):
        with evidence_directory() as d:
            with self.assertRaises(ValueError):
                probe.local_file(pathlib.Path(d), '../outside.bin')


class CallpathChecks(unittest.TestCase):
    def test_direct_path(self):
        graph = {'nodes': [{'rva': 10, 'edges': [{'target': 20}]}, {'rva': 20, 'edges': [{'target': 30}]}]}
        self.assertEqual(find_path(graph, 10, 30)['path'], ['0xa', '0x14', '0x1e'])

    def test_missing_indirect_path_is_unknown(self):
        graph = {'nodes': [{'rva': 10, 'edges': [{'target': None}]}]}
        self.assertEqual(find_path(graph, 10, 30)['status'], 'UNKNOWN')


class ProbeChecks(unittest.TestCase):
    def make_run(self, root):
        meta = dict(schema=1, api='D3D11', feature_id=18, route='native', runtime_modified=False,
                    bypass_used=False, init_result=1, create_result=1, device_removed=False,
                    validation_errors=0, colour_space='linear', runtime_sha256='a' * 64,
                    host_sha256='b' * 64, driver='test', gpu='test', adapter_id='test',
                    caller_module='test', parameter_provider='test', contract_file='contract.json',
                    width=1, height=1, format='RGBA8_UNORM', row_pitch=4)
        contract = dict(render_size=[1, 1], output_size=[1, 1], depth_format='R32_FLOAT',
                        mv_format='RG16_FLOAT', mv_scale=[1, 1], jitter=[0, 0], reset=True,
                        exposure=1, flags=0)
        (root / 'probe.json').write_text(json.dumps(meta))
        (root / 'contract.json').write_text(json.dumps(contract))
        (root / 'before.bin').write_bytes(bytes(4))
        (root / 'a.bin').write_bytes(bytes([1, 0, 0, 255]))
        (root / 'b.bin').write_bytes(bytes([2, 0, 0, 255]))
        frames = [dict(frame=i, evaluate_result=1, fence_id='Q0', fence_target=i + 1,
                       fence_completed=i + 1, before_file='before.bin', output_file='a.bin' if i < 30 else 'b.bin',
                       input_case='a' if i < 30 else 'b') for i in range(60)]
        return meta, frames

    def run_case(self, mutate):
        with evidence_directory() as d:
            root = pathlib.Path(d)
            meta, frames = self.make_run(root)
            mutate(meta, frames)
            (root / 'probe.json').write_text(json.dumps(meta))
            (root / 'frames.jsonl').write_text('\n'.join(json.dumps(f) for f in frames))
            return probe.validate(root)

    def test_complete_still_needs_review(self):
        r = self.run_case(lambda m, f: None)
        self.assertEqual(r['status'], 'EVIDENCE_COMPLETE_REQUIRES_REVIEW')
        self.assertEqual(r['state_layout'], 'UNVERIFIED')

    def test_pending_gpu_rejected(self):
        r = self.run_case(lambda m, f: f[0].update(fence_completed=0))
        self.assertTrue(any('GPU completion' in x for x in r['issues']))

    def test_device_removed_sentinel_rejected(self):
        r = self.run_case(lambda m, f: f[0].update(fence_completed=(1 << 64) - 1))
        self.assertTrue(any('GPU completion' in x for x in r['issues']))

    def test_duplicate_frame_rejected(self):
        r = self.run_case(lambda m, f: f[1].update(frame=0))
        self.assertTrue(any('consecutive' in x for x in r['issues']))

    def test_bridge_not_native(self):
        r = self.run_case(lambda m, f: m.update(route='bridge'))
        self.assertTrue(any('bridge success' in x for x in r['issues']))

    def test_no_write_rejected(self):
        r = self.run_case(lambda m, f: f[0].update(output_file='before.bin'))
        self.assertTrue(any('unchanged' in x for x in r['issues']))

    def test_wrong_feature_rejected(self):
        r = self.run_case(lambda m, f: m.update(feature_id=1))
        self.assertTrue(any('Feature 18' in x for x in r['issues']))


if __name__ == '__main__':
    unittest.main()
