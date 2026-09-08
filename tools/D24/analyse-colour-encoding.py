"""Replay input encoding on captured SR pixels; counterfactuals do not rerun NR."""
import argparse,json
from pathlib import Path
import numpy as np

def srgb(v):
    v=np.clip(v,0,1)
    return np.where(v<=.0031308,v*12.92,1.055*np.power(v,1/2.4)-.055)
def linear(v):
    return np.where(v<=.04045,v/12.92,((v+.055)/1.055)**2.4)
def chroma(v):return v/np.maximum(v.sum(axis=-1,keepdims=True),1e-8)
def curve(v,mode):
    if mode=='Classic':
        y=v@np.array([.2126,.7152,.0722],np.float32)
        rolled=.75+.25*(-np.expm1(-np.maximum(y-.75,0)/.25))
        return v*np.where(y>.75,rolled/np.maximum(y,1e-8),1)[:,None]
    peak=np.max(v,axis=-1)
    if mode=='Hybrid':
        excess=np.maximum(peak-.75,0)/.25
        enc=np.where(peak<=.75,peak,.75+.25*excess/np.sqrt(1+excess*excess))
    else:enc=peak/np.sqrt(1+peak*peak)
    return v*(enc/np.maximum(peak,1e-8))[:,None]
def analyse(path):
    meta=json.loads(path.read_text());n=meta['width']*meta['height'];raw=path.with_suffix('.bin')
    if raw.stat().st_size!=n*48:raise ValueError('Incomplete capture')
    # Same deterministic full-screen sampling grid for all stages; limit analysis memory.
    data=[];offset=0
    for dt in ('<f4','<f2','<f2','<f4'):
        a=np.memmap(raw,mode='r',dtype=dt,offset=offset,shape=(n,4))
        data.append(np.asarray(a[::4,:3],dtype=np.float32));offset+=n*4*np.dtype(dt).itemsize
    original,proxy,model,output=data
    pl,ml=linear(proxy),linear(model)
    normalized=original/meta['whitePoint'];classic=curve(normalized,'Classic');expected=srgb(classic)
    red=(original[:,0]>2*original[:,1])&(original[:,0]>2*original[:,2])&(original[:,0]>1)
    groups={}
    for name,mask in [('all',np.ones(len(original),dtype=bool)),('bright_red',red)]:
        if not mask.any():continue
        values={}
        for stage,v in [('sr',original),('proxy',pl),('model',ml),('composed',output)]:
            values[stage]={'median_chromaticity':np.median(chroma(v[mask]),axis=0).tolist()}
        values['proxy_ceiling_channel_fraction']=(proxy[mask]>=.999).mean(axis=0).tolist()
        values['classic_preclamp_channel_above_one_fraction']=(classic[mask]>1).mean(axis=0).tolist()
        for a,b,v1,v2 in [('sr','proxy',original,pl),('proxy','model',pl,ml),('sr','composed',original,output)]:
            values[a+'_'+b+'_chroma_delta_p50_p95']=np.percentile(np.max(np.abs(chroma(v1[mask])-chroma(v2[mask])),axis=-1),[50,95]).tolist()
        groups[name]={'pixels':int(mask.sum()),**values}
    candidates=[]
    for wp in sorted(set([meta['whitePoint'],4.,16.,float(meta['preExposure'])])):
        for mode in ('Classic','Hybrid','Neutwo'):
            encoded=srgb(curve(original/wp,mode));decoded=linear(encoded)
            candidates.append({'whitePoint':wp,'encoding':mode,
                'near_ceiling_fraction':np.mean(encoded>=.999,axis=0).tolist(),
                'median_proxy_luminance':float(np.median(decoded@np.array([.2126,.7152,.0722],np.float32))),
                'sr_proxy_chroma_delta_p95':float(np.percentile(np.max(np.abs(chroma(original)-chroma(decoded)),axis=-1),95))})
    return {'frame':meta['frame'],'sample_stride':4,'preExposure':meta['preExposure'],
        'classic_replay_abs_error_p50_p95_p99_max':np.percentile(np.abs(expected-proxy),[50,95,99,100]).tolist(),
        'groups':groups,'input_only_counterfactuals':candidates,
        'limitations':['Bright-red mask is an objective colour selection, not semantic fire segmentation.',
            'Counterfactuals change input only; no model/output quality prediction.',
            'Using preExposure as a candidate white point is not a calibrated exposure correction.']}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    result=[analyse(f) for f in sorted(a.directory.glob('*.json')) if f.with_suffix('.bin').exists()]
    a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    for r in result:print(json.dumps({k:r[k] for k in ('frame','classic_replay_abs_error_p50_p95_p99_max','groups')}))
