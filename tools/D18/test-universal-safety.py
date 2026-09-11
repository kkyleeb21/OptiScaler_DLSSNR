from pathlib import Path
import importlib.util,json,unittest
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
summary=module('summary',HERE/'summarize-diagnostics.py')
review=module('review',HERE/'analyse-capture-evidence.py')
class SafetyIntegration(unittest.TestCase):
 def test_shared_write_failure_summary(self):
  event={'type':'capture_write_failed','frame':100,'reason':'metadata_write','result':0,'sequence':7,'flags':0,'fence_target':0}
  s=summary.summarize({'game':'fixture','backend':'dx12','session':1,'dropped':0},[event])
  self.assertEqual(s['first_anomaly']['type'],'capture_write_failed')
  self.assertEqual(s['capture_write_failures'],[{'frame':100,'reason':'metadata_write'}])
  self.assertIn('Capture write failures',summary.markdown(s))
 def test_real_warp_capture_is_readable(self):
  data=review.analyse(ROOT/'tests/write-fixtures/success')
  self.assertEqual(len(data['frames']),1)
  self.assertTrue(data['frames'][0]['eligible'])
  self.assertEqual(data['frames'][0]['alpha_max_delta'],0)
 def test_partial_writes_never_pass_analyser(self):
  fixtures=ROOT/'tests/write-fixtures'
  for name in ('raw_open','raw_short','raw_close','metadata_open','metadata_close','metadata_error','manifest_open','manifest_close'):
   with self.subTest(name=name):
    with self.assertRaises(OSError):review.analyse(fixtures/name)
 def test_production_entry_uses_shared_policies(self):
  s=(ROOT/'core-source/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')
  # Wiring checks supplement executable helper/GPU tests, not standalone gameplay evidence.
  prep=s[s.index('NrRetirementPreparation PrepareNrRetirement'):s.index('void ForgetRetiredCommandListUses')]
  self.assertNotIn('queueHint',prep);self.assertIn('ObservedLastUse(',prep)
  dispatch=s[s.index('void DlssNr_Dx12::DispatchLocked'):s.index('namespace DlssNr\n',s.index('void DlssNr_Dx12::DispatchLocked'))]
  self.assertLess(dispatch.index('EnsureNativeSubmissionObserver(device)'),dispatch.index('ScopedNrStateEnvelope stateEnvelope'))
  self.assertIn('formatChanged, guidesChanged)',dispatch)
  handoff=s[s.index('void EvaluateAfterUpscale('):s.index('UiSnapshot ReadUiSnapshot()')]
  self.assertEqual(handoff.count('nrLock(g_nrMutex)'),1)
  self.assertLess(handoff.index('nrLock(g_nrMutex)'),handoff.index('++g_srHandoffFrames'))
  self.assertIn('PublishNrStatusOnExit publish;',handoff)
  getters=s[s.index('UiSnapshot ReadUiSnapshot()'):s.index('void RequestCapture(')]
  self.assertNotIn('g_nr.',getters);self.assertNotIn('g_nrRetired',getters)
if __name__=='__main__':unittest.main()

