"""Focused E v2 checks; uses production C++ output and bounded synthetic pixels."""
import importlib.util,json,unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('legacy_tests',HERE/'test-capture-evidence.py')
legacy=importlib.util.module_from_spec(spec);spec.loader.exec_module(legacy)
review=legacy.review
ROOT=HERE.parents[2]/'tests'
class Observations(unittest.TestCase):
    def setUp(self):
        self.path=legacy.fixture(ROOT/'observation-fixtures'/self._testMethodName)
        self.production=json.loads((ROOT/'observations.json').read_text())
        for i in range(3):
            e=dict(self.production);e['frame']=100+i;e['successful_since_reset']=40+i
            (self.path/f'evidence_{i:02d}.json').write_text(json.dumps(e))
    def alter(self,i,**changes):
        p=self.path/f'evidence_{i:02d}.json';e=json.loads(p.read_text());e.update(changes);p.write_text(json.dumps(e))
    def test_actual_serializer_keeps_rr_separate(self):
        r=review.analyse(self.path);e=r['frames'][0]['evidence']
        self.assertTrue(e['rr']);self.assertFalse(e['route_rr']);self.assertEqual(e['rr_source'],'ngx_feature')
        self.assertFalse(e['resolve_branches']['matched_residual_eligible'])
        self.assertEqual(sum(p['eligible'] for p in r['temporal_pairs']),2)
    def test_branch_change_breaks_contract(self):
        self.alter(1,resolve_branches={'evidence':'cpu_constants_not_gpu_trace','matched_residual_eligible':True})
        self.assertFalse(any(p['eligible'] for p in review.analyse(self.path)['temporal_pairs']))
    def test_source_change_breaks_contract(self):
        self.alter(1,rr_source='route_contract')
        self.assertFalse(any(p['eligible'] for p in review.analyse(self.path)['temporal_pairs']))
    def test_missing_v2_field_rejected(self):
        p=self.path/'evidence_00.json';e=json.loads(p.read_text());del e['resolve_branches'];p.write_text(json.dumps(e))
        with self.assertRaises(ValueError):review.analyse(self.path)
    def test_hybrid_change_breaks_contract(self):
        self.alter(1,highlight_encoding=1)
        r=review.analyse(self.path)
        self.assertFalse(any(p["eligible"] for p in r["temporal_pairs"]))
    def test_bypass_rejects_hybrid(self):
        self.alter(0,passthrough=1,highlight_encoding=1)
        with self.assertRaises(ValueError):review.analyse(self.path)
    def test_unsupported_dx12_encoding_rejected(self):
        self.alter(0,highlight_encoding=2)
        with self.assertRaises(ValueError):review.analyse(self.path)
    def test_bad_extent_rejected(self):
        self.alter(0,model_input_extent=[0,5])
        with self.assertRaises(ValueError):review.analyse(self.path)
    def test_legacy_remains_readable_but_not_equivalent(self):
        old=review.analyse(legacy.fixture(ROOT/'observation-fixtures/legacy'))
        current=review.analyse(self.path);comparison=review.compare(old,current)
        self.assertIn('rr_source',comparison['pairs'][0]['changed_contract_fields'])
        self.assertTrue(any('Legacy v1 RR labels are unverified' in x for x in old['limitations']))
    def test_encoding_events_in_shared_summary(self):
        spec=importlib.util.spec_from_file_location('summary',HERE/'summarize-diagnostics.py')
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        events=[{'type':t,'frame':100,'reason':reason,'result':mode,'flags':0,'fence_target':0}
                for t,reason,mode in [('highlight_encoding','hybrid',1),('highlight_encoding_unavailable','classic_retained',0)]]
        r=m.summarize({'game':'fixture','backend':'dx12','session':1,'dropped':0},events)
        self.assertEqual(len(r['highlight_encoding_events']),2)
        self.assertIn('classic_retained',m.markdown(r))
    def test_shared_summary_supports_v2(self):
        spec=importlib.util.spec_from_file_location('summary',HERE/'summarize-diagnostics.py')
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        r=m.summarize({'game':'fixture','backend':'dx12','session':1,'dropped':0},[])
        m.add_capture(r,self.path);self.assertEqual(r['capture_evidence']['frames'][0]['evidence']['rr_source'],'ngx_feature')
if __name__=='__main__':unittest.main()
