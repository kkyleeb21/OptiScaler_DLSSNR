"""Validate real GPU capture packets and packed guide decoder coverage."""
import argparse,json,tempfile
from pathlib import Path
import numpy as np
from capture_formats import read_crop

p=argparse.ArgumentParser();p.add_argument('--analysis',type=Path,required=True);p.add_argument('--guide-log',type=Path,required=True);a=p.parse_args()
frames=json.loads(a.analysis.read_text())
assert len(frames)==12
assert {x['metadata']['source_formats']['sr'] for x in frames}=={2,10,26}
for frame in frames:
    for key,check in frame['guide_checks'].items():
        assert check['coverage']=='compared',(key,check)
        assert check['nonfinite_components']==0,(key,check)
        assert check['max_absolute_delta']<=1e-7,(key,check)
    m=frame['metadata'];assert m['call']>0 and m['epoch']>0
    assert m['capture_end']['colour_mask']==15
    assert m['capture_end']['result']==1
    assert m['model_params']['DLSSNR.ScalingRatio']['value']==m['ratio']
rows=[json.loads(line) for line in a.guide_log.read_text().splitlines() if 'dx11_crop' in line]
assert {x['format'] for x in rows}=={19,33,34,15,41,16}
for row in rows:
    v=read_crop(a.guide_log.parent,row)
    assert np.all(v[...,0]==.25),row
    if row['stage'].startswith('motion'):assert np.all(v[...,1]==-.5),row
with tempfile.TemporaryDirectory(dir=Path(__file__).parents[2]/'builds',prefix='capture-formats-') as temp:
    root=Path(temp);name='D24Capture_packed.raw'
    for fmt,dt,value in [(53,'<u2',32768),(56,'<u2',32768),(44,'<u4',0xab800000)]:
        (root/name).write_bytes(np.array([value],dtype=dt).tobytes())
        v=read_crop(root,dict(file=name,width=1,height=1,format=fmt,ok=1))
        assert abs(float(v[0,0,0])-.5)<1e-4
    try:read_crop(root,dict(file='../escape',width=1,height=1,format=41,ok=1));raise AssertionError('path accepted')
    except ValueError:pass
    try:read_crop(root,dict(file=name,width=2,height=1,format=44,ok=1));raise AssertionError('truncated accepted')
    except ValueError:pass
print('PASS: 12 complete GPU frames; raw/owned/post guides; signed typeless inputs; D16/D24; invalid paths/sizes')
