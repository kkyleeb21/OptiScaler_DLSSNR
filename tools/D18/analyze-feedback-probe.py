"""Validate bounded real-NR feedback host captures and report temporal measurements."""
import argparse,hashlib,json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('runs',nargs='+',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
rows=[];ids=[];input_hash=None;style=None;contract=None
pass_count=json.loads((a.runs[0]/'manifest.json').read_text())['passes']
assert pass_count in [2,3]
fig,axes=plt.subplots(1,pass_count,figsize=(5.5*pass_count,4),layout='constrained')
for root in a.runs:
 m=json.loads((root/'manifest.json').read_text());assert m['schema']=='d18-feedback-probe-v1' and m['complete'] and m['exit_code']==0
 assert m['saved_frames']==list(range(48,64)) and m['width']==640 and m['height']==384 and m['passes'] in [2,3]
 current={k:m[k] for k in ['passes','ratio','frames_evaluated','warmup_frames','static_input','motion_zero_all_passes','depth','driver_core_sha256','runtime_sha256','executable_sha256','cross_frame_wait']}
 current['frozen_input_sha256']=m.get('frozen_input_sha256')
 current['input_schedule']=m.get('input_schedule','constant')
 if contract is None:contract=current
 assert current==contract,'Host contract or binary identity mismatch'
 if style is None:style=m['style']
 assert style==m['style'],'Compare identical style only'
 assert len(m['files'])==1+16*m['passes']+int('frozen_input_sha256' in m)
 expected_names={'input_0.rgba16f'}|{f'pass{s}_{f}.rgba16f' for s in range(1,m['passes']+1) for f in m['saved_frames']}
 if 'frozen_input_sha256' in m:
  expected_names.add('frozen_0.rgba16f');assert m['files']['frozen_0.rgba16f']==m['frozen_input_sha256']
 assert set(m['files'])==expected_names
 for name,expected in m['files'].items():
  file=root/name;assert not file.is_symlink() and file.stat().st_size==640*384*8
  assert hashlib.sha256(file.read_bytes()).hexdigest().upper()==expected
 if input_hash is None:input_hash=m['files']['input_0.rgba16f']
 assert input_hash==m['files']['input_0.rgba16f'],'Input mismatch'
 def load(name):
  v=np.fromfile(root/name,dtype='<f2').astype('f4').reshape(384,640,4);assert np.isfinite(v).all();return v[:,:,:3]@np.array([.2126,.7152,.0722])
 mask=load('input_0.rgba16f')<.09
 for s in range(1,m['passes']+1):
  y=np.stack([load(f'pass{s}_{f}.rgba16f') for f in m['saved_frames']]);v=y[:,mask].mean(axis=1)
  x=np.arange(16,dtype=float);X=np.column_stack([np.ones(16),x]);trend=X@np.linalg.lstsq(X,v,rcond=None)[0]
  row=dict(run=root.name,mode=m['mode'],reset_policy=m.get('reset_policy','initial'),input_schedule=m.get('input_schedule','constant'),measurement_kind='step_response' if m.get('input_schedule')=='step48' else 'temporal_variation',passes=m['passes'],stage=s,style=style,dark_pixels=int(mask.sum()),
           mean_pixel_temporal_std=float(y[:,mask].std(axis=0).mean()),mean_luma=[float(i) for i in v],
           mean_peak_to_peak_fraction=float(np.ptp(v)/max(float(v.mean()),1e-8)),detrended_mean_std=float((v-trend).std()))
  row['runtime_data_intervention']=m.get('runtime_data_intervention',False)
  row['counter_policy']=m.get('counter_policy','per_pass' if m.get('runtime_data_intervention') else None)
  rows.append(row)
  label=m['mode']+(' / reset='+m['reset_policy'] if 'reset_policy' in m else '')
  if m.get('runtime_data_intervention'):label+=' / '+row['counter_policy']+' (diagnostic)'
  axes[s-1].plot(m['saved_frames'],100*(v/v.mean()-1),'.-',label=label);axes[s-1].set_title(f'Pass {s} | style={style}')
 ids.append(dict({k:m[k] for k in ['mode','style','passes','executable_sha256','driver_core_sha256','runtime_sha256','start','end']},runtime_data_intervention=m.get('runtime_data_intervention',False),run=str(root.resolve())))
for ax in axes:ax.grid(alpha=.3);ax.set_xlabel('Host frame');ax.set_ylabel('Dark mean deviation (%)');ax.legend(fontsize=8)
fig.savefig(a.output/'feedback-temporal.png',dpi=150);plt.close(fig)
result=dict(schema='d18-feedback-analysis-v1',identities=ids,input_sha256=input_hash,input_schedule=contract['input_schedule'],measurements=rows,
            boundary='Synthetic input with constant guides, 48 warmup and 16 measured frames; step48 changes the base at frame 48 without a scene Reset. Step response variation is not flicker evidence. Readback and per-frame CPU waits alter scheduling. No internal history capture or game-quality verdict.')
(a.output/'analysis.json').write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
for v in rows:print(v['mode'],v['stage'],'dark_std',v['mean_pixel_temporal_std'],'swing',v['mean_peak_to_peak_fraction'],'detrended',v['detrended_mean_std'])
