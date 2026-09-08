"""Four-stage native capture analysis. Full-frame by default; optional pixel ROI."""
import argparse, json, importlib.util
from pathlib import Path
import numpy as np
spec=importlib.util.spec_from_file_location('frequency',Path(__file__).resolve().parents[1]/'D18/analyze-capture-frequency.py')
frequency=importlib.util.module_from_spec(spec);spec.loader.exec_module(frequency)
spec=importlib.util.spec_from_file_location('formats',Path(__file__).with_name('capture_formats.py'))
formats=importlib.util.module_from_spec(spec);spec.loader.exec_module(formats)

def import_dx11(log, output):
    """Adapt captured DX11 formats to the existing four-stage packet; require complete frames."""
    frames={};contracts={};contexts={};ends={};progress=[]
    for line in log.read_text(encoding='utf-8-sig').splitlines():
        try:r=json.loads(line)
        except ValueError:continue
        if r.get('event')=='dx11_capture_contract':contracts[r['frame']]=r
        if r.get('event')=='dx11_frame_context':contexts[r['frame']]=r
        if r.get('event')=='dx11_capture_frame_end':ends[(r['run'],r['call'])]=r
        if r.get('event')=='dx11_capture_progress':progress.append(r)
        if r.get('event')=='dx11_crop':frames.setdefault(r['frame'],{})[r['stage']]=r
    output.mkdir(parents=True,exist_ok=True);packets=[]
    for frame,entries in frames.items():
        if not all(n in entries and entries[n]['ok'] for n in ('sr','input','model','composed')) or frame not in contracts:continue
        shape={(entries[n]['width'],entries[n]['height'],entries[n]['x'],entries[n]['y']) for n in ('sr','input','model','composed')}
        if len(shape)!=1:raise ValueError('Stage regions do not match')
        w,h,x,y=shape.pop();stages={}
        for name in ('sr','input','model','composed'):
            r=entries[name]
            v=formats.read_crop(log.parent,r)
            if v.shape!=(h,w,4):raise ValueError('Invalid colour stage shape')
            stages[name]=v
        m=dict(contracts[frame],width=w,height=h,x=x,y=y,api='DX11',source_formats={n:entries[n].get('format',10) for n in stages})
        if frame in contexts:
            m.update({k:v for k,v in contexts[frame].items() if k!='event'})
            m['capture_end']=ends.get((m['run'],m['call']))
        m['guides']={}
        for name in ('depth_raw','motion_raw','depth_owned','motion_owned','depth_after','motion_after','motion_nr','motion_nr_after'):
            if name not in entries:
                m['guides'][name]={'coverage':'not_observed'};continue
            r=entries[name]
            try:
                a=formats.read_crop(log.parent,r);dest=output/f'dx11-{frame}-{name}.npy';np.save(dest,a)
                m['guides'][name]=dict(r,coverage='captured',packet_file=dest.name,statistics=formats.statistics(a))
            except (OSError,ValueError) as exc:m['guides'][name]=dict(r,coverage='unavailable',reason=str(exc))
        if 'exposure' in entries and entries['exposure']['ok']:
            r=entries['exposure'];raw=(log.parent/r['file']).read_bytes()
            m['exposure_raw']={'format':r['format'],'width':r['width'],'height':r['height'],'hex':raw.hex(),'float32_interpretation':np.frombuffer(raw,dtype='<f4').tolist(),'semantics':'Raw observation; not an assertion of exposure channel meaning.'}
        packet=output/f'dx11-{frame}.json';packet.write_text(json.dumps(m,indent=2),encoding='utf-8')
        packet.with_suffix('.bin').write_bytes(b''.join(stages[n].astype(dtype).tobytes() for n,dtype in [('sr','<f4'),('input','<f2'),('model','<f2'),('composed','<f4')]))
        packets.append(packet)
    if not packets:raise ValueError('No complete four-stage frames with matching metadata')
    (output/'coverage.json').write_text(json.dumps({'complete_colour_frames':len(packets),'observed_frame_groups':len(frames),'progress':progress,'limitations':['Complete colour does not imply guide coverage or gameplay correctness.','Diagnostic readback changes frame timing.']},indent=2),encoding='utf-8')
    return packets

def decode(v):
    v=np.clip(v,0,1)
    return np.where(v<=.04045,v/12.92,((v+.055)/1.055)**2.4)

def analyse(path, roi=None):
    m=json.loads(path.read_text(encoding='utf-8-sig'))
    w,h=m['width'],m['height'];pixels=w*h
    raw=path.with_suffix('.bin')
    if raw.stat().st_size!=pixels*48: raise ValueError('Incomplete capture or unsupported layout')
    stages={};offset=0
    for name,dtype in [('sr','<f4'),('proxy','<f2'),('model','<f2'),('composed','<f4')]:
        view=np.memmap(raw,dtype=dtype,mode='r',offset=offset,shape=(h,w,4))
        offset+=pixels*4*np.dtype(dtype).itemsize
        if roi:
            x,y,rw,rh=roi
            if min(x,y)<0 or min(rw,rh)<=0 or x+rw>w or y+rh>h: raise ValueError('ROI outside frame')
            view=view[y:y+rh,x:x+rw]
        stages[name]=np.asarray(view[...,:3],dtype=np.float32)
    result={'metadata':m,'roi':roi,'stages':{},'comparisons':{}}
    result['jitter_checks']={}
    correction=m.get('jitter_correction',{})
    if correction.get('active'):
        entries=m.get('guides',{})
        selected=[entries.get(k,{}) for k in ('motion_owned','motion_nr','motion_nr_after')]
        if all(e.get('coverage')=='captured' for e in selected) and all(all(e.get(k)==selected[0].get(k) for k in ('x','y','width','height')) for e in selected):
            loaded=[]
            for e in selected:
                name=e['packet_file']
                if Path(name).name!=name:raise ValueError('Invalid guide path')
                loaded.append(np.load(path.parent/name,allow_pickle=False))
            base,nr,after=loaded
            if base.shape==nr.shape==after.shape:
                expected=base+np.array([correction['raw_offset_x'],correction['raw_offset_y']],dtype=np.float32)
                valid=np.isfinite(base)&np.isfinite(nr)&np.isfinite(after)
                result['jitter_checks']={'coverage':'compared','nonfinite_components':int((~valid).sum()),'correction_max_error':float(np.max(np.abs(nr[valid]-expected[valid]))) if valid.any() else None,'post_model_max_delta':float(np.max(np.abs(after[valid]-nr[valid]))) if valid.any() else None}
        if not result['jitter_checks']:result['jitter_checks']={'coverage':'incomplete_or_mismatched_guides'}
    else:result['jitter_checks']={'coverage':'inactive'}
    result['guide_checks']={}
    for kind in ('depth','motion'):
        for left,right in (('raw','owned'),('owned','after')):
            a=m.get('guides',{}).get(kind+'_'+left,{});b=m.get('guides',{}).get(kind+'_'+right,{})
            key=f'{kind}_{left}_{right}'
            if a.get('coverage')!='captured' or b.get('coverage')!='captured':result['guide_checks'][key]={'coverage':'not_observed'};continue
            if any(a.get(k)!=b.get(k) for k in ('x','y','width','height')):result['guide_checks'][key]={'coverage':'region_mismatch'};continue
            def guide(entry):
                name=entry['packet_file']
                if Path(name).name!=name:raise ValueError('Invalid guide path')
                return np.load(path.parent/name,allow_pickle=False)
            va,vb=guide(a),guide(b)
            if va.shape!=vb.shape:result['guide_checks'][key]={'coverage':'shape_mismatch'};continue
            valid=np.isfinite(va)&np.isfinite(vb);delta=np.abs(va[valid]-vb[valid])
            result['guide_checks'][key]={'coverage':'compared','nonfinite_components':int((~valid).sum()),'max_absolute_delta':float(delta.max()) if delta.size else None,'mean_absolute_delta':float(delta.mean()) if delta.size else None}
    linear={}
    for name,v in stages.items():
        finite=np.all(np.isfinite(v),axis=-1)
        clean=v[finite]
        result['stages'][name]={'nonfinite_pixels':int((~finite).sum()),
            'negative_pixels':int(np.any(clean<0,axis=-1).sum()),
            'channel_ge_one_fraction':np.mean(clean>=1,axis=0).tolist() if len(clean) else None,
            'rgb_percentiles':np.percentile(clean,[1,50,95,99,100],axis=0).tolist() if len(clean) else None}
        linear[name]=decode(v) if name in ('proxy','model') and not m['passthrough'] else v
        luma=frequency.luminance(linear[name])
        result['stages'][name]['luminance_bands']=frequency.bands(luma)
        result['stages'][name]['brightness_normalized_bands']=frequency.bands(luma/max(float(np.mean(np.abs(luma))),1e-8))
    for a,b in [('sr','proxy'),('proxy','model'),('sr','composed')]:
        va,vb=linear[a],linear[b]
        sa,sb=va.sum(axis=-1),vb.sum(axis=-1)
        valid=(sa>1e-5)&(sb>1e-5)&np.all(np.isfinite(va)&np.isfinite(vb),axis=-1)
        ca=va[valid]/sa[valid,None];cb=vb[valid]/sb[valid,None]
        delta=np.max(np.abs(cb-ca),axis=-1)
        result['comparisons'][a+'_'+b]={'valid_pixels':int(valid.sum()),
            'chromaticity_max_channel_delta_p50_p95_p99':np.percentile(delta,[50,95,99]).tolist() if len(delta) else None}
        result['comparisons'][a+'_'+b]['luminance_difference_bands']=frequency.bands(frequency.luminance(vb)-frequency.luminance(va))
    result['limitations']=['Proxy/model interpreted using captured passthrough flag; not display screenshots.',
        'Values >=1 are not proof of clipping for linear HDR SR/composed stages.',
        'Different encodings have different luminance distributions. Compare hue and brightness separately.',
        'High-frequency change can be detail or noise; consecutive fixed-scene frames are needed.',
        'Exposure raw pixels, if present, do not by themselves establish channel semantics.']
    # DX11 'transfer' selects composition (classic/matched residual), not the sRGB encoding.
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('--roi',nargs=4,type=int,metavar=('X','Y','W','H'))
    p.add_argument('--out',type=Path);p.add_argument('--dx11-import',type=Path,help='Import a DX11 log to this packet directory');a=p.parse_args()
    captures=import_dx11(a.capture,a.dx11_import) if a.dx11_import else [a.capture]
    results=[analyse(path,a.roi) for path in captures]
    text=json.dumps(results if a.dx11_import else results[0],indent=2,ensure_ascii=False)
    if a.out:a.out.write_text(text,encoding='utf-8')
    else:print(text)
