"""Runtime layout observations must not imply gameplay success."""
from pathlib import Path
import sys,json,uuid,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from summarize_runtime import summarize
class LayoutSummaryTests(unittest.TestCase):
    def parse(self,row):
        p=Path(__file__).resolve().parents[3]/'builds'/('layout-summary-'+uuid.uuid4().hex)
        p.mkdir(parents=True);log=p/'native.log';log.write_text(json.dumps(row),encoding='utf-8')
        return summarize(log)
    def test_observed(self):
        row=dict(event='runtime_layout',rule='d18-dx11-host-3108-v1',accepted=True,reason='host_layout_compatible',region='none',offset=0)
        r=self.parse(row);self.assertEqual(r['runtime_layout_checks'],[row]);self.assertEqual(r['runtime_layout_coverage'],'observed');self.assertEqual(r['gameplay_verdict'],'not_measured')
    def test_missing(self):
        r=self.parse(dict(event='native_init',value=1));self.assertEqual(r['runtime_layout_checks'],[]);self.assertEqual(r['runtime_layout_coverage'],'not_observed')
if __name__=='__main__':unittest.main()
