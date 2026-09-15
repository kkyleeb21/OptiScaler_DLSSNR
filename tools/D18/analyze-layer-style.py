"""Read-only same-frame model-stage colour/contrast measurements, never game-quality acceptance."""
import argparse,importlib.util,json,struct
from pathlib import Path
import numpy as np
spec=importlib.util.spec_from_file_location('layer_capture',Path(__file__).with_name('analyze-layer-capture.py'))
capture=importlib.util.module_from_spec(spec);spec.loader.exec_module(capture)
def analyze(root):
 root=Path(root);checked,_=capture.inspect(root)
 if not checked['complete']:raise ValueError('Complete capture required for stage comparison')
 m=json.loads((root/'manifest.json').read_text(encoding='utf-8-sig'))
 if any(s['shared'] for s in m['samples']):raise ValueError('Independent-history scope only')
 rows=[]
 for f,sm in enumerate(m['samples']):
  words=bytes.fromhex(sm['resolve_constants_hex']);passthrough=struct.unpack_from('<I',words,32)[0]
  for roi in range(5):
   stages=[]
   for stage in [1,2]:
    dtype='<f2' if m['resources'][stage]['format']==10 else '<f4'
    v=np.frombuffer((root/f'f{f:02}_s{stage}_r{roi}.raw').read_bytes(),dtype=dtype).astype(np.float64).reshape(128,128,4)[...,:3]
    if not passthrough:
     v=np.clip(v,0,1);v=np.where(v<.04045,v/12.92,((v+.055)/1.055)**2.4)
    stages.append(v)
   a,b=stages;ya=a@np.array([.2126,.7152,.0722]);yb=b@np.array([.2126,.7152,.0722]);mask=(ya>.003)&(yb>.003)
   ca=a/np.maximum(a.sum(axis=2,keepdims=True),1e-9);cb=b/np.maximum(b.sum(axis=2,keepdims=True),1e-9)
   rows.append(dict(frame=sm['frame'],region=roi,valid_chroma_pixels=int(mask.sum()),
    mean_rgb_pass1=a.mean(axis=(0,1)).tolist(),mean_rgb_pass2=b.mean(axis=(0,1)).tolist(),
    mean_luma_ratio=float(yb.mean()/max(ya.mean(),1e-9)),
    normalized_contrast_pass1=float((np.percentile(ya,90)-np.percentile(ya,10))/max(ya.mean(),1e-9)),
    normalized_contrast_pass2=float((np.percentile(yb,90)-np.percentile(yb,10))/max(yb.mean(),1e-9)),
    chroma_share_shift=(cb[mask]-ca[mask]).mean(axis=0).tolist() if mask.any() else None,
    mean_absolute_chroma_share_change=float(np.abs(cb[mask]-ca[mask]).mean()) if mask.any() else None))
 summary=[]
 for roi in range(5):
  group=[v for v in rows if v['region']==roi]
  summary.append(dict(region=roi,mean_luma_ratio=float(np.median([v['mean_luma_ratio'] for v in group])),
   normalized_contrast_ratio=float(np.median([v['normalized_contrast_pass2']/max(v['normalized_contrast_pass1'],1e-9) for v in group])),
   chroma_share_shift=np.mean([v['chroma_share_shift'] for v in group if v['chroma_share_shift'] is not None],axis=0).tolist()))
 return dict(schema='d18-layer-style-v1',source=str(root.resolve()),complete=True,sha256=checked['sha256'],
  capture_parameters=m['samples'][0],regions=m['regions'],summary=summary,samples=rows,
  boundary='Same-frame pass1/pass2 captured outputs only. Linearized using production saturating sRGB decode when needed. RGB shares are not perceptual hue. No final composition replay, no guides/history, no proof of current-game skin distortion or natural inevitability.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 v=analyze(a.capture);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(v,indent=2,allow_nan=False),encoding='utf-8');print(json.dumps(v['summary'],indent=2))
