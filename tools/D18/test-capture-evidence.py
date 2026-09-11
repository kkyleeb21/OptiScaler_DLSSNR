"""New E regression: reject unmatched/incomplete captures instead of ranking them."""
import importlib.util, json, unittest
from pathlib import Path
import numpy as np
spec=importlib.util.spec_from_file_location('review',Path(__file__).with_name('analyse-capture-evidence.py'))
review=importlib.util.module_from_spec(spec);spec.loader.exec_module(review)

def fixture(root,frames=3):
    root.mkdir(parents=True,exist_ok=True);w,h=7,5;pitch=128
    (root/'manifest.txt').write_text(f'frames {frames}\n'+''.join(f'{s} width {w} height {h} format 2 rowPitch {pitch}\n' for s in review.STREAMS))
    for i in range(frames):
        e={'schema':'d18-capture-evidence-v1','api':'d3d12','frame':100+i,'successful_since_reset':40+i,'reset':False,
           'rr':False,'guides_captured':False,'exact_cross_run_replay':False,'network':[4,3],
           'output_rect':[1,1,5,3],'depth_rect':[0,0,4,3],'motion_rect':[0,0,4,3],
           'pre_exposure':1,'model':[1,1,1,1,0,0,0],'white_point':1,'passthrough':1,
           'transfer':1,'debug_view':0,'compare_mode':0,'resolve_constants_hex':'00'}
        (root/f'evidence_{i:02d}.json').write_text(json.dumps(e))
        for s in review.STREAMS:
            pixels=np.zeros((h,pitch//4),dtype='<f4')
            a=pixels[:,:w*4].reshape(h,w,4);a[:,:,:3]=np.arange(w)[None,:,None]*0.1+0.2;a[:,:,3]=0.5
            # Deliberately fill row padding with non-finite bytes: decoder must ignore it.
            pixels[:,w*4:]=np.nan
            (root/f'{s}_{i:02d}.raw').write_bytes(pixels.tobytes()[:pitch*(h-1)+w*16])
    return root

class EvidenceTests(unittest.TestCase):
    def setUp(self):
        # Fixed bounded fixtures stay in the candidate test output, not OS temp.
        self.root=fixture(Path(__file__).resolve().parents[3]/'tests/fixtures'/self._testMethodName)
    def alter(self,index,**changes):
        p=self.root/f'evidence_{index:02d}.json';e=json.loads(p.read_text());e.update(changes);p.write_text(json.dumps(e))
    def test_identity_padded_odd_surface(self):
        r=review.analyse(self.root);self.assertEqual(sum(p['eligible'] for p in r['temporal_pairs']),2)
        for f in r['frames']:
            self.assertEqual(f['edit_luma_rms'],0);self.assertEqual(f['alpha_max_delta'],0)
            self.assertEqual(f['outside_rect_rgb_max_delta'],0)
    def test_no_claim_of_exact_replay(self):
        a=review.analyse(self.root);r=review.compare(a,a)
        self.assertTrue(all(p['sr_colour_bytes_match'] for p in r['pairs']))
        self.assertFalse(any(p['exact_same_input_history'] for p in r['pairs']))
    def test_reset_and_warmup(self):
        self.alter(1,reset=True,successful_since_reset=1)
        self.alter(2,successful_since_reset=2)
        self.assertFalse(any(p['eligible'] for p in review.analyse(self.root)['temporal_pairs']))
    def test_gap_or_configuration_change(self):
        self.alter(1,frame=105)
        self.alter(2,white_point=2)
        self.assertFalse(any(p['eligible'] for p in review.analyse(self.root)['temporal_pairs']))
    def test_debug_not_quality(self):
        self.alter(0,debug_view=1)
        self.assertFalse(review.analyse(self.root)['frames'][0]['eligible'])
    def test_different_colour_detected(self):
        a=review.analyse(self.root);p=self.root/'before_00.raw';data=bytearray(p.read_bytes());data[:4]=np.float32(0.9).tobytes();p.write_bytes(data)
        self.assertEqual(review.compare(a,review.analyse(self.root))['pairs'][0]['classification'],'different_sr_input')
    def test_incomplete_stream_rejected(self):
        p=self.root/'after_00.raw';p.write_bytes(p.read_bytes()[:-1])
        with self.assertRaises(ValueError):review.analyse(self.root)
    def test_nonfinite_active_pixel_rejected(self):
        p=self.root/'after_00.raw';data=bytearray(p.read_bytes());data[:4]=np.float32(np.nan).tobytes();p.write_bytes(data)
        with self.assertRaises(ValueError):review.analyse(self.root)
    def test_missing_evidence_rejected(self):
        (self.root/'evidence_00.json').unlink()
        with self.assertRaises(OSError):review.analyse(self.root)
    def test_invalid_rectangle_rejected(self):
        self.alter(0,output_rect=[6,4,3,3])
        with self.assertRaises(ValueError):review.analyse(self.root)
    def test_excessive_capture_rejected(self):
        p=self.root/'manifest.txt';p.write_text(p.read_text().replace('frames 3','frames 99'))
        with self.assertRaises(ValueError):review.analyse(self.root)
    def test_formats(self):
        for fmt,dt,values,expected in [(10,'<f2',[0.5,0.25,1,0.5],[0.5,0.25,1,0.5]),
                (24,'<u4',[1023|(1023<<20)|(3<<30)],[1,0,1,1]),
                (28,'u1',[255,0,128,255],[1,0,128/255,1]),(87,'u1',[255,0,0,255],[0,0,1,1]),
                (26,'<u4',[(15<<6)|((15<<6)<<11)|((15<<5)<<22)],[1,1,1])]:
            p=self.root/'format.raw';p.write_bytes(np.array(values,dtype=dt).tobytes())
            result,_=review.read_surface(p,(1,1,fmt,p.stat().st_size))
            np.testing.assert_allclose(result[0,0],expected,atol=1e-6)
    def test_production_serializer(self):
        p=Path(__file__).resolve().parents[3]/'tests/production-metadata.json'
        e=review.strict_json(p)
        self.assertEqual(e['schema'],'d18-capture-evidence-v2')
        self.assertEqual(e['white_point'],2.5);self.assertEqual(e['successful_since_reset'],40)
        self.assertEqual(e['output_rect'],[3,2,11,9]);self.assertFalse(e['guides_captured'])
        self.assertEqual(len(bytes.fromhex(e['resolve_constants_hex'])),172)
    def test_shared_summary_entry(self):
        spec=importlib.util.spec_from_file_location('summary',Path(__file__).with_name('summarize-diagnostics.py'))
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        result=module.summarize({'game':'fixture','backend':'dx12','session':1,'dropped':0},[])
        module.add_capture(result,self.root)
        self.assertEqual(len(result['capture_evidence']['frames']),3)
        self.assertIn('Matched frame capture',module.markdown(result))
    def test_actual_warp_capture(self):
        p=Path(__file__).resolve().parents[3]/'tests/gpu-capture'
        result=review.analyse(p)
        self.assertEqual(len(result['frames']),1)
        frame=result['frames'][0]
        self.assertTrue(frame['eligible']);self.assertEqual(frame['evidence']['network'],[9,7])
        self.assertLess(frame['edit_luma_rms'],0.00002)
        self.assertEqual(frame['outside_rect_rgb_max_delta'],0)

if __name__=='__main__':unittest.main()
