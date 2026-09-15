"""Regression for merged render + allocation events, using synthetic schema-v1 records."""
from pathlib import Path
import importlib.util,json,sys,unittest
path=Path(__file__).resolve().parents[1]/'summarize-render-diagnostics.py'
spec=importlib.util.spec_from_file_location('render_summary',path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def record(kind,reason='',flags=0,result=1):
 r=dict.fromkeys(['sequence','monotonic_ms','frame','feature_generation','queue','command_list','fence_target','fence_completed','width','height','network_width','network_height','guide_width','guide_height'],0)
 r.update(type=kind,reason=reason,flags=flags,result=result,ratio=.5,exposure=1,white_point=1,mv_scale_x=1,mv_scale_y=1);return r
class MergeTests(unittest.TestCase):
 def test_allocation_failure_survives(self):
  r=record('allocation_failed','scratch',result=0x8007000e);s=m.summarize({},[r]);self.assertEqual(s['first_anomaly'],r);self.assertEqual(s['allocation_failures'][0]['role'],'scratch')
 def test_device_loss_survives(self):
  s=m.summarize({},[record('device_lost',result=0x887a0005)]);self.assertTrue(s['allocation_failures'][0]['device_lost'])
 def test_off_and_overflow_are_not_render_failures(self):
  s=m.summarize({},[record('nr_skip','it is switched off'),record('nr_outcome','summary_overflow;n=7',result=0)]);self.assertIsNone(s['first_anomaly']);self.assertEqual(s['nr_outcome_summary']['signature_overflow_attempts'],7);self.assertEqual(s['nr_outcome_summary']['observed_attempts'],0)
 def test_outcomes_and_queue_keep_evidence_boundary(self):
  s=m.summarize({},[record('nr_outcome','ok;n=3;s=7',flags=(2<<12)|(2<<16)|(2<<20)|(2<<24)),record('nr_queue_history','promoted')]);self.assertEqual(s['nr_outcome_summary']['requested_to_composed'],{'2->2':3});self.assertIn('not_current_gpu_completion',s['nr_queue_history'][0]['evidence'])
 def test_missing_is_not_success(self):
  s=m.summarize({},[]);self.assertIn('unobserved',s['nr_outcome_summary']['coverage']);self.assertEqual(s['nr_memory'],[])
 def test_old_game_summary_unchanged(self):
  root=Path('E:/DLSSNR');old=json.loads((root/'evidence/D18/NTE_C7_layers_20260913/multipass-summary.json').read_text(encoding='utf-8-sig'));h,r=m.read_ring(root/'evidence/D18/NTE_C7_layers_20260913/D18Diagnostics.ring');new=m.summarize(h,r)
  for k in old:self.assertEqual(old[k],new[k],k)
  self.assertEqual(new['nr_outcome_summary']['observed_attempts'],3211);self.assertEqual(new['nr_outcome_summary']['reduced_pass_attempts'],0)
if __name__=='__main__':unittest.main()
