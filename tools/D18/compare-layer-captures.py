"""Compare two verified cropped stage sequences without assuming identical scene pixels.
All spatial/temporal metrics are observational, not a flicker verdict or exact replay.
"""
import argparse,importlib.util,json,hashlib
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

spec=importlib.util.spec_from_file_location('layer_analysis',Path(__file__).with_name('analyze-layer-capture.py'))
analysis=importlib.util.module_from_spec(spec);spec.loader.exec_module(analysis)

def compare(left,right,out):
    out=Path(out);out.mkdir(parents=True,exist_ok=False)
    dirs=[Path(left),Path(right)];labels=['A','B'];summaries=[analysis.inspect(p)[0] for p in dirs]
    manifests=[json.loads((p/'manifest.json').read_text()) for p in dirs]
    if not all(s['complete'] for s in summaries):raise ValueError('Pair comparison requires complete sequences')
    if manifests[0]['regions']!=manifests[1]['regions'] or manifests[0]['resources']!=manifests[1]['resources']:raise ValueError('Stage/ROI contract differs')
    ignored={'frame','attempt','pre_exposure','shared','later_pass_zero_mv'}
    keys=set(manifests[0]['samples'][0])|set(manifests[1]['samples'][0])
    differences={k:[manifests[0]['samples'][0].get(k),manifests[1]['samples'][0].get(k)] for k in sorted(keys-ignored)
                 if manifests[0]['samples'][0].get(k)!=manifests[1]['samples'][0].get(k)}
    drift={label:[k for k in sorted(keys-ignored) if any(sm.get(k)!=m['samples'][0].get(k) for sm in m['samples'])]
           for label,m in zip(labels,manifests)}
    pixels={};rows=[];series={};luma=np.array([.2126,.7152,.0722],np.float32)
    for label,p,m in zip(labels,dirs,manifests):
        for r in range(5):
            means=[]
            for s,d in enumerate(m['resources']):
                dtype='<f2' if d['format']==10 else '<f4'
                rgb=np.stack([np.fromfile(p/f'f{f:02}_s{s}_r{r}.raw',dtype=dtype).astype(np.float32).reshape(128,128,4)[:,:,:3] for f in range(m['frames'])])
                pixels[label,s,r]=rgb
                y=rgb@luma;v=y.mean(axis=(1,2));means.append(v)
                row=dict(group=label,region=r,stage=m['stages'][s],mean_luma=[float(x) for x in v],
                         roi_mean_peak_to_peak_fraction=float(np.ptp(v)/max(abs(float(v.mean())),1e-6)),
                         mean_pixel_temporal_std=float(y.std(axis=0).mean()))
                rows.append(row);series[label,s,r]=v
            # Within each frame, SR and final share the same game domain; ratio of ROI means
            # helps separate source illumination from changed composition, but is not an error truth.
            gain=means[3]/np.maximum(means[0],1e-6)
            rows.append(dict(group=label,region=r,stage='final_to_sr_roi_mean_gain',values=[float(v) for v in gain],
                             peak_to_peak_fraction=float(np.ptp(gain)/max(abs(float(gain.mean())),1e-6))))
    selection={label:dict(path=str(p.resolve()),shared=m['samples'][0]['shared'],frames=[s['frame'] for s in m['samples']],
               source=m['samples'][0]['source'],reset_masks=[s['reset_mask'] for s in m['samples']],
               pre_exposure=[s['pre_exposure'] for s in m['samples']],manifest_sha256=summary['sha256']['manifest.json'])
               for label,p,m,summary in zip(labels,dirs,manifests,summaries)}
    stable=[]
    for r in range(5):
        bases={label:pixels[label,0,r]@luma for label in labels}
        # Explicit common mask: reject bright/near-zero values, strong temporal source changes,
        # and different average source content across the two runs. It is still not motion alignment.
        mask=np.ones((128,128),bool)
        for y in bases.values():mask&=(y.mean(axis=0)>.005)&(y.mean(axis=0)<.1)&(y.std(axis=0)<.001)
        mask&=np.abs(bases['A'].mean(axis=0)-bases['B'].mean(axis=0))<.01
        for label in labels:
            for s in range(4):
                y=pixels[label,s,r]@luma
                if not mask.any():continue
                v=y[:,mask].mean(axis=1)
                stable.append(dict(group=label,region=r,stage=manifests[0]['stages'][s],pixels=int(mask.sum()),
                    mean_luma=[float(x) for x in v],roi_mean_peak_to_peak_fraction=float(np.ptp(v)/max(abs(float(v.mean())),1e-6)),
                    mean_pixel_temporal_std=float(y[:,mask].std(axis=0).mean())))
    result=dict(schema='d18-layer-comparison-v1',selection=selection,first_sample_parameter_differences=differences,
                within_group_parameter_drift=drift,metrics=rows,
                stable_source_mask=dict(mean_range=[.005,.1],temporal_std_below=.001,cross_run_mean_difference_below=.01,
                                        boundary='Common fixed-coordinate mask; not optical-flow alignment or proof of no input change',metrics=stable),
                boundary='Different capture times and scene pixels; rain/motion/exposure are confounds. Crops and only 8 frames cannot establish a full-screen root cause. Model proxy stages are not subtracted from game-domain images.')
    (out/'comparison.json').write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
    # Every ROI shown; no hidden region selection or per-group scaling.
    fig,axes=plt.subplots(5,2,figsize=(12,15),layout='constrained')
    for r in range(5):
        for col,label in enumerate(labels):
            ax=axes[r,col]
            for s,name in enumerate(manifests[0]['stages']):
                v=series[label,s,r];ax.plot(np.arange(len(v)),100*(v/v.mean()-1),'.-',label=name)
            ax.set_title(f'{label} | shared={selection[label]["shared"]} | ROI {r}')
            ax.set_ylabel('ROI mean deviation (%)');ax.set_xlabel('Captured frame index');ax.grid(alpha=.25)
        lo=min(axes[r,0].get_ylim()[0],axes[r,1].get_ylim()[0]);hi=max(axes[r,0].get_ylim()[1],axes[r,1].get_ylim()[1])
        for ax in axes[r]:ax.set_ylim(lo,hi)
    axes[0,0].legend(fontsize=8);fig.savefig(out/'temporal-means.png',dpi=150);plt.close(fig)
    fig,axes=plt.subplots(5,4,figsize=(12,14),layout='constrained')
    for r in range(5):
        for col,(label,s) in enumerate([('A',0),('A',3),('B',0),('B',3)]):
            # Direct clamped game RGB visualization, not a calibrated HDR display transform.
            axes[r,col].imshow(np.clip(pixels[label,s,r][0],0,1));axes[r,col].axis('off')
            axes[r,col].set_title(f'{label} {manifests[0]["stages"][s]} ROI {r}',fontsize=9)
    fig.suptitle('First-frame crops | direct clamped game RGB (not calibrated HDR)')
    fig.savefig(out/'crop-contact-sheet.png',dpi=150);plt.close(fig)
    print(json.dumps({'parameter_differences':differences,'within_group_drift':drift,'complete':True}))
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('left');p.add_argument('right');p.add_argument('--output',required=True);a=p.parse_args()
    compare(a.left,a.right,a.output)
