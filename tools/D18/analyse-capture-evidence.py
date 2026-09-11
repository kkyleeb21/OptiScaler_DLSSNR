"""Bounded D18 four-stream capture review. No game control or replay claims.

Same-frame before/after is a paired observation. Identical SR colour across runs
does not prove identical guides, exposure textures or model history.
"""
from __future__ import annotations
import argparse, hashlib, importlib.util, json, re
from pathlib import Path
import numpy as np

spec=importlib.util.spec_from_file_location('frequency',Path(__file__).with_name('analyze-capture-frequency.py'))
frequency=importlib.util.module_from_spec(spec);spec.loader.exec_module(frequency)
STREAMS=('before','after','model_input','model_output')
BASE_STABLE=('rr','network','output_rect','depth_rect','motion_rect','pre_exposure','model',
        'white_point','resolve_constants_hex')
OBSERVED=('rr_source','route_rr','model_input_extent','input_kernel','highlight_encoding','resolve_branches')
STABLE=BASE_STABLE+OBSERVED

def strict_json(path):
    def invalid(value):raise ValueError('non-finite JSON: '+value)
    return json.loads(path.read_text(encoding='utf-8'),parse_constant=invalid)

def manifest(path):
    text=(path/'manifest.txt').read_text(encoding='utf-8')
    count=re.search(r'^frames (\d+)$',text,re.M)
    if not count or not 1<=int(count[1])<=8:raise ValueError('invalid or unbounded frame count')
    layouts={}
    for key,w,h,fmt,pitch in re.findall(r'^(before|after|model_input|model_output) width (\d+) height (\d+) format (\d+) rowPitch (\d+)$',text,re.M):
        if key in layouts:raise ValueError('duplicate stream')
        layouts[key]=tuple(map(int,(w,h,fmt,pitch)))
    if set(layouts)!=set(STREAMS):raise ValueError('incomplete four-stream manifest')
    return int(count[1]),layouts

def read_surface(path,layout):
    w,h,fmt,pitch=layout
    formats={2:('<f4',4),10:('<f2',4),26:('<u4',1),24:('<u4',1),28:('u1',4),87:('u1',4)}
    if fmt not in formats:raise ValueError('unsupported colour format '+str(fmt))
    dt,channels=formats[fmt];item=np.dtype(dt).itemsize;stride=w*channels*item
    if not(0<w<=16384 and 0<h<=16384 and stride<=pitch and pitch%item==0 and pitch*h<=512*1024*1024):
        raise ValueError('invalid or excessive surface size')
    # D3D12 GetCopyableFootprints may omit trailing padding on the final row.
    size=path.stat().st_size;minimum=pitch*(h-1)+stride
    if not minimum<=size<=pitch*h:raise ValueError('incomplete/oversized raw surface: '+path.name)
    raw=np.memmap(path,mode='r',dtype='u1',shape=(size,))
    rows=np.ndarray((h,stride),dtype='u1',buffer=raw,strides=(pitch,1)).copy()
    digest=hashlib.sha256(rows.tobytes()).hexdigest()
    pixels=rows.view(dt).reshape(h,w,channels)
    if fmt==26:
        packed=pixels[...,0]
        pixels=np.stack((frequency.decode_ufloat(packed&2047,6),frequency.decode_ufloat((packed>>11)&2047,6),
                         frequency.decode_ufloat((packed>>22)&1023,5)),axis=-1)
    elif fmt==24:
        packed=pixels[...,0];pixels=np.stack([(packed>>shift)&1023 for shift in (0,10,20)]+[(packed>>30)&3],axis=-1).astype(np.float32)
        pixels/=np.array([1023,1023,1023,3],dtype=np.float32)
    elif fmt in (28,87):
        pixels=pixels.astype(np.float32)/255
        if fmt==87:pixels=pixels[...,[2,1,0,3]]
    else:pixels=pixels.astype(np.float32)
    if not np.isfinite(pixels).all():raise ValueError('non-finite pixels: '+path.name)
    return pixels,digest

def rms(value):return float(np.sqrt(np.mean(np.square(value,dtype=np.float64))))
def evidence(path,index,layout):
    e=strict_json(path/f'evidence_{index:02d}.json')
    if e.get('schema') not in ('d18-capture-evidence-v1','d18-capture-evidence-v2') or e.get('api')!='d3d12':raise ValueError('missing/unsupported evidence schema')
    required=set(BASE_STABLE)|{'frame','reset','successful_since_reset','passthrough','debug_view','compare_mode','guides_captured','exact_cross_run_replay'}
    if required-set(e):raise ValueError('incomplete frame evidence')
    if e['schema']=='d18-capture-evidence-v2':
        if set(OBSERVED)-set(e):raise ValueError('incomplete v2 observations')
        if e['rr_source'] not in ('ngx_feature','route_contract') or type(e['rr'])!=bool or type(e['route_rr'])!=bool:
            raise ValueError('invalid source observations')
        if len(e['model_input_extent'])!=2 or any(type(v)!=int or not 0<v<=16384 for v in e['model_input_extent']):
            raise ValueError('invalid model input extent')
        if e['resolve_branches'].get('evidence')!='cpu_constants_not_gpu_trace' or type(e['resolve_branches'].get('matched_residual_eligible'))!=bool:
            raise ValueError('invalid branch observations')
        if e['highlight_encoding'] not in (0,1) or type(e['highlight_encoding'])!=int or type(e['input_kernel'])!=int or not 0<=e['input_kernel']<=2:
            raise ValueError('unsupported effective encoding/kernel')
        if e['passthrough']!=0 and e['highlight_encoding']!=0:
            raise ValueError('encoded mode on bypass input')
    w,h=layout[:2];x,y,rw,rh=e['output_rect']
    if any(type(v)!=int for v in (x,y,rw,rh)) or min(x,y)<0 or min(rw,rh)<=0 or x+rw>w or y+rh>h:
        raise ValueError('invalid output rectangle')
    if type(e['frame'])!=int or type(e['successful_since_reset'])!=int or min(e['frame'],e['successful_since_reset'])<0:
        raise ValueError('invalid history/frame evidence')
    if type(e['reset'])!=bool:raise ValueError('invalid reset')
    return e

def analyse(directory,min_warmup=32):
    path=Path(directory);count,layouts=manifest(path)
    if layouts['before'][:2]!=layouts['after'][:2]:raise ValueError('before/after dimensions differ')
    if layouts['model_input'][:2]!=layouts['model_output'][:2]:raise ValueError('model boundary dimensions differ')
    rows=[];pairs=[];previous=None
    for index in range(count):
        e=evidence(path,index,layouts['before'])
        arrays={};hashes={}
        for stream in STREAMS:arrays[stream],hashes[stream]=read_surface(path/f'{stream}_{index:02d}.raw',layouts[stream])
        x,y,w,h=e['output_rect'];crop=(slice(y,y+h),slice(x,x+w))
        before=arrays['before'][crop][...,:3];after=arrays['after'][crop][...,:3]
        lb,la=frequency.luminance(before),frequency.luminance(after);edit=la-lb
        bband,aband=frequency.bands(lb),frequency.bands(la)
        bright=lb>=np.quantile(lb,0.95)
        eligible=not e['reset'] and e['successful_since_reset']>=min_warmup and e['debug_view']==0 and e['compare_mode']==0
        row={'index':index,'evidence':e,'pixel_sha256':hashes,'eligible':eligible,
            'before_bands':bband,'after_bands':aband,'edit_luma_rms':rms(edit),
            'high_frequency_ratio':aband['high_rms']/bband['high_rms'] if bband['high_rms']>1e-12 else None,
            'highlight_edit_mean':float(edit[bright].mean()),'highlight_edit_rms':rms(edit[bright]),
            'model_boundary_edit_rms':rms(arrays['model_output'][...,:3]-arrays['model_input'][...,:3])}
        # Format quantization can contribute to this value; it is an observation, not an automatic failure verdict.
        outside=np.ones(layouts['before'][1::-1],dtype=bool);outside[crop]=False
        row['outside_rect_rgb_max_delta']=float(np.max(np.abs(arrays['after'][...,:3][outside]-arrays['before'][...,:3][outside]))) if outside.any() else None
        if arrays['before'].shape[-1]==4 and arrays['after'].shape[-1]==4:
            row['alpha_max_delta']=float(np.max(np.abs(arrays['after'][...,3]-arrays['before'][...,3])))
        else:row['alpha_max_delta']=None
        if previous:
            pe,plb,pla,peligible=previous
            stable=all(pe.get(k)==e.get(k) for k in STABLE)
            contiguous=e['frame']==pe['frame']+1 and e['successful_since_reset']==pe['successful_since_reset']+1
            valid=eligible and peligible and stable and contiguous
            pair={'a':index-1,'b':index,'eligible':valid,'same_contract':stable,'contiguous':contiguous}
            if valid:
                pair.update(before_delta_rms=rms(lb-plb),after_delta_rms=rms(la-pla),edit_delta_rms=rms(edit-(pla-plb)))
            pairs.append(pair)
        rows.append(row);previous=(e,lb.copy(),la.copy(),eligible)
    return {'schema':'d18-capture-review-v1','directory':str(path.resolve()),'frames':rows,'temporal_pairs':pairs,
        'minimum_successful_evaluations':min_warmup,'layouts':layouts,
        'limitations':['Same-frame SR/NR pairs are available; guides and exact model history are not replayed.',
            'Frequency or temporal change is descriptive: sharper/noisier and motion/flicker require visual review.',
            'Eight captured frames are a short burst, not a long-duration temporal or performance benchmark.',
            'Pixel values are in the captured game domain; this is not display-calibrated HDR analysis.',
            'Legacy v1 RR labels are unverified; absent branch observations are unknown, not disabled.',
            'V2 branch eligibility follows CPU resolve constants, not a GPU execution trace.']}

def compare(a,b):
    rows=[]
    for left,right in zip(a['frames'],b['frames']):
        e,f=left['evidence'],right['evidence']
        matches=left['pixel_sha256']['before']==right['pixel_sha256']['before'] and a['layouts']['before']==b['layouts']['before']
        rows.append({'a':left['index'],'b':right['index'],'sr_colour_bytes_match':matches,
            'changed_contract_fields':[key for key in STABLE if e.get(key)!=f.get(key)],
            'both_warmed_up':left['eligible'] and right['eligible'],
            'exact_same_input_history':False,
            'classification':'sr_colour_matched_only' if matches else 'different_sr_input'})
    return {'pairs':rows,'frame_counts_match':len(a['frames'])==len(b['frames']),
            'conclusion':'No strict cross-run quality ranking: guides/history are not captured or replayed.'}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('capture',type=Path)
    parser.add_argument('--compare',type=Path);parser.add_argument('--min-warmup',type=int,default=32)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.min_warmup<1:parser.error('--min-warmup must be positive')
    try:
        report=analyse(args.capture,args.min_warmup)
        if args.compare:report['cross_run']=compare(report,analyse(args.compare,args.min_warmup))
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf-8')
    except (ValueError,OSError,KeyError,TypeError) as error:parser.exit(2,str(error)+'\n')
    print(f"Reviewed {len(report['frames'])} paired frames; eligible temporal pairs: {sum(p['eligible'] for p in report['temporal_pairs'])}")
if __name__=='__main__':main()
