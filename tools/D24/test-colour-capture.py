import importlib.util, json, tempfile
from pathlib import Path
import numpy as np

spec=importlib.util.spec_from_file_location('capture',Path(__file__).with_name('analyse-colour-capture.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='d18-colour-test-',dir=Path(__file__).resolve().parents[2]/'builds') as tmp:
    path=Path(tmp)/'capture.json'
    path.write_text(json.dumps({'width':4,'height':2,'passthrough':1}),encoding='utf-8')
    sr=np.tile(np.array([.8,.2,.1,1],dtype='<f4'),(2,4,1))
    proxy=sr.astype('<f2');model=proxy.copy();model[:,:,1]=.6
    composed=model.astype('<f4')
    raw=path.with_suffix('.bin');raw.write_bytes(sr.tobytes()+proxy.tobytes()+model.tobytes()+composed.tobytes())
    result=module.analyse(path)
    assert result['comparisons']['proxy_model']['chromaticity_max_channel_delta_p50_p95_p99'][0]>.1
    assert module.analyse(path,[1,0,2,2])['comparisons']['sr_proxy']['valid_pixels']==4
    raw.write_bytes(sr.tobytes()+proxy.tobytes()+proxy.tobytes()+sr.tobytes())
    assert module.analyse(path)['comparisons']['sr_composed']['chromaticity_max_channel_delta_p50_p95_p99']==[0,0,0]
    try:module.analyse(path,[3,0,2,1]);raise AssertionError('bad ROI accepted')
    except ValueError:pass
    raw.write_bytes(b'incomplete')
    try:module.analyse(path);raise AssertionError('incomplete capture accepted')
    except ValueError:pass
print('PASS: colour-change attribution, unchanged output, ROI and truncated-capture rejection')
