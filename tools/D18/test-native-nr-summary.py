"""Offline classification checks; no GPU or game configuration changes."""
import importlib.util
import json
from pathlib import Path
import uuid
import unittest

spec=importlib.util.spec_from_file_location('native_summary',Path(__file__).with_name('summarize-native-nr.py'))
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class SummaryTests(unittest.TestCase):
    def summarize(self, rows):
        directory=Path(__file__).resolve().parent/('.native-summary-test-'+uuid.uuid4().hex)
        directory.mkdir()
        path=directory/'native.log'
        try:
            path.write_text('\n'.join(json.dumps(r) for r in rows),encoding='utf-8')
            return module.summarize(path)
        finally:
            path.unlink(missing_ok=True)
            directory.rmdir()

    def test_shared_rejection_retains_raw_event_and_does_not_claim_recovery(self):
        event={'event':'skip_or_failure','value':-30}
        result=self.summarize([event,{'event':'create','value':1}])
        self.assertEqual(result['errors'],[event])
        self.assertEqual(result['findings'][0]['category'],'configuration_fallback')
        self.assertFalse(result['observation_coverage']['advanced_frames'])
        self.assertEqual(result['completed_model_frame_observations'],[])

    def test_completion_failure_is_not_shared_mismatch(self):
        event={'event':'completion_failed','value':-12}
        result=self.summarize([event])
        self.assertEqual(result['errors'],[event])
        self.assertNotIn('shared_parameters_mismatch',[f['code'] for f in result['findings']])

    def test_mixed_history_keeps_failure_and_later_observation(self):
        later={'event':'native_advanced','requested':4,'recorded':4,'result':1}
        result=self.summarize([{'event':'native_advanced','result':-30},later])
        self.assertEqual(result['findings'][0]['observations'],1)
        self.assertTrue(result['observation_coverage']['advanced_frames'])
        self.assertEqual(result['advanced_frame_observations'][-1],later)
        self.assertFalse(result['observation_coverage']['model_completion'])

    def test_empty_log_is_missing_evidence(self):
        result=self.summarize([])
        self.assertEqual(result['errors'],[])
        self.assertFalse(any(result['observation_coverage'].values()))
        self.assertEqual(result['findings'][0]['category'],'evidence_gap')

if __name__=='__main__':unittest.main()
