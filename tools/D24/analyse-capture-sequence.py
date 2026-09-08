"""Compare complete four-stage packets over time, reuse shared frequency/colour analysis."""
import argparse, json, importlib.util
from pathlib import Path
import numpy as np

CONTRACT_KEYS=('width','height','x','y','ratio','transfer','white_point_scale',
    'exposure_used','pre_exposure_used','transfer_strength','colour_strength',
    'intensity','local_structure','local_tone','style','preset','skin_structure',
    'custom_filter','passthrough','debug_view','compare','run','epoch','flags',
    'auto_mask','linear_resolve','linear_input','use_exposure','owner','context')

def compatible(a,b):
    if not all(a.get(k)==b.get(k) for k in CONTRACT_KEYS):return False
    for m in (a,b):
        if m.get('model_params',{}).get('DLSSNR.Reset',{}).get('value'):return False
    return True

def motion_jitter_fit(pairs):
    """Describe observed coupling, without asserting a model's input contract."""
    def fit(group):
        if len(group)<3:return {'count':len(group),'coverage':'insufficient_pairs'}
        jitter=np.asarray([p['jitter_delta_xy'] for p in group],dtype=float)
        motion=np.asarray([p['scaled_motion_roi_median_xy'] for p in group],dtype=float)
        axes=[]
        for axis in range(2):
            x,y=jitter[:,axis],motion[:,axis];valid=np.isfinite(x)&np.isfinite(y);x,y=x[valid],y[valid]
            if len(x)<3 or np.std(x)<1e-8:axes.append({'coverage':'insufficient_jitter_variation'});continue
            slope,intercept=np.linalg.lstsq(np.column_stack((x,np.ones_like(x))),y,rcond=None)[0]
            axes.append({'coverage':'measured','slope':float(slope),'intercept':float(intercept),
                'correlation':float(np.corrcoef(x,y)[0,1]) if np.std(y)>1e-8 else None,
                'motion_rms':float(np.sqrt(np.mean(y*y))),
                'motion_plus_jitter_delta_rms':float(np.sqrt(np.mean((x+y)**2)))})
        return {'count':len(group),'first_frame':group[0]['a'],'last_frame':group[-1]['b'],'xy':axes}
    bursts=[]
    for pair in pairs:
        if not bursts or bursts[-1][-1]['b']!=pair['a']:bursts.append([])
        bursts[-1].append(pair)
    return {'all':fit(pairs),'bursts':[fit(b) for b in bursts],
        'limitations':['Fits use scaled ROI medians, not tracked static geometry.',
        'A slope near -1 supports jitter in motion vectors; it does not prove NR must cancel it or that cancellation fixes flicker.']}

spec=importlib.util.spec_from_file_location('capture',Path(__file__).with_name('analyse-colour-capture.py'))
capture=importlib.util.module_from_spec(spec);spec.loader.exec_module(capture)

def load(path):
    m=json.loads(path.read_text());w,h=m['width'],m['height'];n=w*h;offset=0;v={}
    if path.with_suffix('.bin').stat().st_size!=n*48:raise ValueError('Incomplete packet')
    for name,dt in [('sr','<f4'),('input','<f2'),('model','<f2'),('composed','<f4')]:
        v[name]=np.asarray(np.memmap(path.with_suffix('.bin'),mode='r',dtype=dt,offset=offset,shape=(h,w,4))[...,:3],dtype=np.float32)
        offset+=n*4*np.dtype(dt).itemsize
    return m,v

def analyse(paths):
    frames=[];pairs=[];previous=None;guide_pairs=[]
    for path in paths:
        m,raw=load(path)
        v={k:capture.decode(a) if k in ('input','model') and not m['passthrough'] else a for k,a in raw.items()}
        lum={k:capture.frequency.luminance(a) for k,a in v.items()}
        scale=max(float(np.mean(np.abs(lum['sr']))),1e-8)
        frames.append({'frame':m['frame'],'metadata':m,'stage_bands':{k:capture.frequency.bands(a) for k,a in lum.items()},
                       'model_edit_bands':capture.frequency.bands(lum['model']-lum['input']),
                       'final_edit_bands':capture.frequency.bands(lum['composed']-lum['sr'])})
        if previous:
            pm,pl,ps=previous
            same=compatible(m,pm)
            if m['frame']==pm['frame']+1 and same:
                norm=max((scale+ps)/2,1e-8)
                sr_delta=lum['sr']-pl['sr'];out_delta=lum['composed']-pl['composed']
                pairs.append({'a':pm['frame'],'b':m['frame'],
                    'sr_relative_rms':float(np.sqrt(np.mean(sr_delta**2))/norm),
                    'output_relative_rms':float(np.sqrt(np.mean(out_delta**2))/norm),
                    'model_edit_temporal_bands':capture.frequency.bands((lum['model']-lum['input'])-(pl['model']-pl['input'])),
                    'final_edit_temporal_bands':capture.frequency.bands(out_delta-sr_delta)})
                def observed(meta,group,key):
                    value=meta.get(group,{}).get(key,{})
                    return value.get('value') if value.get('result')==1 else None
                jitter=[observed(m,'game_params','Jitter.Offset.'+axis) for axis in ('X','Y')]
                old_jitter=[observed(pm,'game_params','Jitter.Offset.'+axis) for axis in ('X','Y')]
                scales=[observed(m,'model_params','DLSSNR.MVecScale'+axis) for axis in ('X','Y')]
                entry=m.get('guides',{}).get('motion_owned',{})
                if entry.get('coverage')=='captured' and all(v is not None and np.isfinite(v) for v in jitter+old_jitter+scales):
                    percentiles=entry['statistics']['channel_percentiles']
                    medians=[values[2] if values else None for values in percentiles]
                    guide_pairs.append({'a':pm['frame'],'b':m['frame'],'jitter_delta_xy':np.subtract(jitter,old_jitter).tolist(),
                        'motion_roi_median_xy':medians,'model_mv_scale_xy':scales,
                        'scaled_motion_roi_median_xy':[v*s if v is not None else None for v,s in zip(medians,scales)]})
        previous=m,lum,scale
    return {'frames':frames,'adjacent_pairs_by_sr_stability':sorted(pairs,key=lambda p:p['sr_relative_rms']),'motion_jitter_observations':guide_pairs,'motion_jitter_fit':motion_jitter_fit(guide_pairs),
            'limitations':['Low SR change is a stability proxy, not proof of a motionless camera.',
                            'No motion registration: moving pixels and animation must not be called NR flicker.',
                            'Model and SR/output use different scales; compare each edit in its own domain.',
                            'Motion ROI median and jitter delta do not establish sign, units, or cancellation correctness by themselves.']}

def regional_temporal(paths, regions):
    """Fit local translation from SR only; apply exactly the same warp to all stages.

    Coordinates are packet-local. Residuals include deformation, lighting and
    disocclusion: this is an attribution aid, never an automatic flicker verdict.
    """
    from scipy.ndimage import gaussian_filter, map_coordinates
    from scipy.optimize import minimize
    results=[]; previous=None
    for path in paths:
        m,raw=load(path)
        values={k:capture.frequency.luminance(capture.decode(a) if k in ('input','model') and not m['passthrough'] else a) for k,a in raw.items()}
        if previous:
            pm,pv=previous
            if m['frame']==pm['frame']+1 and compatible(m,pm):
                for name,(x,y,w,h) in regions.items():
                    margin=32
                    if min(x,y)<margin or x+w+margin>m['width'] or y+h+margin>m['height']:
                        raise ValueError('Region needs a 32-pixel registration margin')
                    yy,xx=np.mgrid[y:y+h,x:x+w].astype(float)
                    a=gaussian_filter(pv['sr'],1);b=gaussian_filter(values['sr'],1)
                    norm=max(float(np.mean(a[y:y+h,x:x+w])),1e-8)
                    def objective(offset):
                        delta=map_coordinates(b,[yy[::3,::3]+offset[0],xx[::3,::3]+offset[1]],order=1)-a[y:y+h:3,x:x+w:3]
                        return float(np.mean((delta/norm)**2))
                    coarse=min(((dy,dx) for dy in range(-24,25,4) for dx in range(-24,25,4)),key=objective)
                    fit=minimize(objective,coarse,method='Powell',bounds=[(-32,32),(-32,32)],options={'xtol':.01,'ftol':1e-7})
                    dy,dx=fit.x
                    prev={k:v[y:y+h,x:x+w] for k,v in pv.items()}
                    curr={k:map_coordinates(v,[yy+dy,xx+dx],order=1) for k,v in values.items()}
                    delta={k:curr[k]-prev[k] for k in values}
                    domains={'sr':max(float(np.mean(prev['sr'])),1e-8),'input':max(float(np.mean(prev['input'])),1e-8)}
                    domain={'sr':'sr','composed':'sr','input':'input','model':'input'}
                    stats={k:{'relative_rms':float(np.sqrt(np.mean(d*d))/domains[domain[k]]),
                              'relative_mean_change':float(np.mean(d)/domains[domain[k]]),
                              'relative_high_rms':capture.frequency.bands(d)['high_rms']/domains[domain[k]]} for k,d in delta.items()}
                    results.append({'a':pm['frame'],'b':m['frame'],'region':name,'rect':[x,y,w,h],
                        'sr_translation_dy_dx':[float(dy),float(dx)],'fit_success':bool(fit.success),
                        'fit_at_boundary':bool(max(abs(dy),abs(dx))>31.5),'stages':stats})
        previous=m,values
    return {'pairs':results,'limitations':['Translation is estimated solely from SR; no independent output fitting.',
        'Residuals still contain animation, rotation, occlusion and lighting changes.',
        'Input/model share input mean normalization; SR/composed share SR mean normalization.',
        'Changed capture contracts are excluded. Regions use packet-local coordinates.']}

def preview(paths,destination):
    from PIL import Image,ImageDraw
    loaded=[load(p) for p in paths]
    scale=max(float(np.percentile(np.concatenate([v['sr'][::8,::8].reshape(-1,3) for m,v in loaded]),99)),1e-4)
    tile=192;canvas=Image.new('RGB',(tile*4, (tile+24)*len(loaded)),(24,24,24));draw=ImageDraw.Draw(canvas)
    for i,(m,v) in enumerate(loaded):
        for j,name in enumerate(('sr','input','model','composed')):
            a=v[name]
            if name in ('sr','composed'):a=np.maximum(a,0)/scale;a=1.055*np.power(a,1/2.4)-.055
            im=Image.fromarray(np.uint8(np.clip(a,0,1)*255)).resize((tile,tile))
            canvas.paste(im,(j*tile,i*(tile+24)+24));draw.text((j*tile+4,i*(tile+24)+4),f"{m['frame']} {name}",fill='white')
    canvas.save(destination)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('packets',type=Path);p.add_argument('--out',type=Path,required=True);p.add_argument('--preview',type=Path);p.add_argument('--region',action='append',default=[],help='name:x,y,w,h (packet-local)');a=p.parse_args()
    paths=sorted(a.packets.glob('dx11-*.json'),key=lambda p:int(p.stem.split('-')[-1]))
    if not paths:raise SystemExit('No packets')
    result=analyse(paths)
    if a.region:
        regions={s.split(':',1)[0]:tuple(map(int,s.split(':',1)[1].split(','))) for s in a.region}
        result['regional_temporal']=regional_temporal(paths,regions)
    a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    if a.preview:preview(paths,a.preview)
