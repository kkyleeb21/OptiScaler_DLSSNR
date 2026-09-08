"""Regression checks for evidence levels and the shared adapter entry point."""
import json
import tempfile
import unittest
from pathlib import Path
from summarize_runtime import summarize, load_adapter


class SummaryTests(unittest.TestCase):
    def test_parameter_roundtrip(self):
        row={'event':'nr_parameter','key':'DLSSNR.Intensity','requested':0.375,'readback':0.375,'matched':True}
        self.assertEqual(self.parse(json.dumps(row),'native.log')['parameter_roundtrips'],[row])
        result=self.parse('event=nr_parameter key=DLSSNR.Intensity requested=0.375 readback=0 result=3134193669 matched=0\n')
        self.assertEqual(result['parameter_roundtrips'][0]['matched'],'0')

    def test_allocation_failures(self):
        row={'event':'allocation_failed','role':'output','width':3840,'height':2160,'format':10,'result':2147942414}
        r=self.parse(json.dumps(row),'native.log')
        self.assertEqual(r['allocation_failures'],[row])
        r=self.parse('event=allocation_failed role=output width=3840 height=2160 format=97 result=-2\n')
        self.assertEqual(r['allocation_failures'][0]['result'],'-2')
        m=load_adapter('alloc_ring',Path(__file__).parent.parent/'D18'/'summarize-diagnostics.py')
        event=dict(sequence=1,type='allocation_failed',reason='output',width=3840,height=2160,flags=10,
                   result=2147942414,fence_target=0,queue=0)
        r=m.summarize({},[event])
        self.assertEqual(r['first_anomaly'],event)
        self.assertEqual(r['allocation_failures'][0]['format'],10)
        self.assertFalse(r['allocation_failures'][0]['device_lost'])

    def parse(self, text, name='D24VulkanDiagnostics.log'):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / 'builds', prefix='summary-test-') as folder:
            path = Path(folder) / name
            path.write_text(text, encoding='utf-8')
            return summarize(path)

    def test_conversion_is_not_nr(self):
        r = self.parse('event=nr_frame mode=1 result=1\n')
        self.assertEqual(r['native_execution'], 'conversion_only_recorded')
        self.assertEqual(r['evidence']['model_recordings'], 0)

    def test_model_success_and_failure(self):
        for result, expected in [('1', 'model_recorded'), ('0', 'not_observed')]:
            self.assertEqual(self.parse(f'event=nr_frame mode=2 result={result}\n')['native_execution'], expected)

    def test_legacy_and_empty_are_unknown(self):
        for text in ['', 'event=nr_frame result=1\n']:
            self.assertEqual(self.parse(text)['native_execution'], 'not_observed')

    def test_completion_requires_matching_epoch(self):
        text = 'event=sr_submit epoch=7 result=0\nevent=gpu_completed epoch=8\n'
        self.assertEqual(self.parse(text)['evidence']['sr_submissions_with_completion'], 0)
        r = self.parse(text + 'event=gpu_completed epoch=7\n')
        self.assertEqual(r['evidence']['sr_submissions_with_completion'], 1)
        self.assertEqual(r['evidence']['nr_gpu_completion'], 'not_correlated')
        self.assertEqual(r['gameplay_verdict'], 'not_measured')

    def test_dx11_adapter(self):
        r = self.parse(json.dumps({'event': 'loaded', 'value': 1}), 'native.log')
        self.assertEqual(r['adapter'], 'dx11-json')
        self.assertEqual(r['summary_schema'], 1)

    def test_ring_adapter(self):
        m = load_adapter('test_ring', Path(__file__).parent.parent / 'D18' / 'summarize-diagnostics.py')
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / 'builds', prefix='summary-test-') as folder:
            path = Path(folder) / 'D18Diagnostics.ring'
            path.write_bytes(m.HEADER.pack(b'D18DIAG\0', 1, m.HEADER.size, m.RECORD.size,
                                          1, 1, 0, 0, 0, b'DX12', b'NR', b'test') + bytes(m.RECORD.size))
            r = summarize(path)
            self.assertEqual(r['adapter'], 'd18-ring')
            self.assertEqual(r['record_count'], 0)
            path.write_bytes(b'D18DIAG')
            with self.assertRaises(ValueError):
                summarize(path)


if __name__ == '__main__':
    unittest.main()
