"""Numerical check: SR-derived registration must not erase model-only changes."""
import importlib.util
import json
import tempfile
from pathlib import Path
import numpy as np
from scipy.ndimage import gaussian_filter, shift

spec=importlib.util.spec_from_file_location('sequence',Path(__file__).with_name('analyse-capture-sequence.py'))
seq=importlib.util.module_from_spec(spec);spec.loader.exec_module(seq)
with tempfile.TemporaryDirectory(dir=Path(__file__).parents[2]/'builds',prefix='sequence-test-') as folder:
    root=Path(folder)
    base=gaussian_filter(np.random.default_rng(17).random((192,192)),2).astype(np.float32)+.2
    def packet(frame,offset,edit=0,ratio=1):
        p=root/f'dx11-{frame}.json'
        p.write_text(json.dumps(dict(frame=frame,width=192,height=192,passthrough=1,ratio=ratio)))
        a=shift(base,offset,order=1,mode='nearest');arrays=[]
        for stage,dt in [('sr','<f4'),('input','<f2'),('model','<f2'),('composed','<f4')]:
            rgb=np.repeat((a+(edit if stage in ('model','composed') else 0))[...,None],4,axis=2)
            arrays.append(rgb.astype(dt).tobytes())
        p.with_suffix('.bin').write_bytes(b''.join(arrays));return p
    first=packet(1,(0,0));second=packet(2,(3,-2))
    regions={'test':(48,48,80,80)}
    p=seq.regional_temporal([first,second],regions)['pairs'][0]
    assert np.allclose(p['sr_translation_dy_dx'],[3,-2],atol=.05),p
    assert p['stages']['composed']['relative_rms']<.001,p
    second=packet(2,(3,-2),.05)
    p=seq.regional_temporal([first,second],regions)['pairs'][0]
    assert p['stages']['sr']['relative_rms']<.001,p
    assert p['stages']['model']['relative_rms']>.05,p
    assert p['stages']['composed']['relative_rms']>.05,p
    second=packet(2,(3,-2),ratio=.5)
    assert not seq.regional_temporal([first,second],regions)['pairs']
pairs=[{'a':i,'b':i+1,'jitter_delta_xy':[x,x/2],
        'scaled_motion_roi_median_xy':[-x+.02,-x/2-.01]} for i,x in enumerate((-.5,.25,-.25,.5,.75))]
fitted=seq.motion_jitter_fit(pairs)
assert len(fitted['bursts'])==1
assert all(abs(axis['slope']+1)<1e-10 for axis in fitted['all']['xy'])
assert seq.motion_jitter_fit([])['all']['coverage']=='insufficient_pairs'
print('PASS: known translation; model-only change retained; changed contract excluded; jitter fit and missing evidence')
