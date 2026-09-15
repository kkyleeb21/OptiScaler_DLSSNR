"""Bounded d18-layer-capture-v1 validation and stage/region temporal measurements.
Raw stages have different domains. Statistics locate changes; they do not prove flicker.
"""
import argparse, hashlib, json
from pathlib import Path
import numpy as np

LIMIT=128*1024*1024
def inspect(directory):
    root=Path(directory).resolve(); manifest=root/'manifest.json'
    if manifest.is_symlink() or manifest.stat().st_size>1024*1024: raise ValueError('invalid manifest')
    m=json.loads(manifest.read_text(encoding='utf-8-sig'))
    if m.get('schema')!='d18-layer-capture-v1' or m.get('api')!='d3d12': raise ValueError('unsupported schema/API')
    n=m['frames'];wanted=m['requested_frames']
    if type(n)!=int or type(wanted)!=int or not 0<=n<=wanted<=8: raise ValueError('frame bound')
    if m.get('gpu_complete') is not True: raise ValueError('GPU completion missing')
    if not 0<m['readback_bytes']<=LIMIT: raise ValueError('readback bound')
    regions=m['regions'];resources=m['resources'];samples=m['samples']
    if len(regions)!=5 or len(resources)!=4 or len(samples)!=n: raise ValueError('stage/region/sample count')
    if m['stages']!=['sr_base','pass1','pass2','final'] or m['domains']!=['game','encoded_proxy','encoded_proxy','game']: raise ValueError('stage/domain contract')
    for d in resources:
        if d['format'] not in (2,10) or not 128<=d['width']<=32768 or not 128<=d['height']<=32768: raise ValueError('format/extent')
        for x,y,w,h in regions:
            if any(type(v)!=int for v in (x,y,w,h)) or w!=128 or h!=128 or min(x,y)<0 or x+w>d['width'] or y+h>d['height']: raise ValueError('region bounds')
    files=[manifest];hashes={};total=0;data={};metrics=[]
    complete=m.get('complete') is True
    continuity=True
    for f,sm in enumerate(samples):
        mask=sm['stage_mask']
        if type(mask)!=int or not 0<=mask<=15: raise ValueError('stage mask')
        if f and (sm['source']!=samples[0]['source'] or sm['shared']!=samples[0]['shared'] or sm['attempt']!=samples[f-1]['attempt']+1):continuity=False
        if complete and mask!=15: raise ValueError('false complete stage claim')
        for s,d in enumerate(resources):
            if not mask&(1<<s): continue
            dtype=np.dtype('<f2' if d['format']==10 else '<f4')
            for r,(_,_,w,h) in enumerate(regions):
                p=root/f'f{f:02}_s{s}_r{r}.raw';size=w*h*4*dtype.itemsize;total+=size
                if total>LIMIT or p.is_symlink() or p.stat().st_size!=size: raise ValueError('raw size/link/budget')
                raw=p.read_bytes();hashes[p.name]=hashlib.sha256(raw).hexdigest().upper();files.append(p)
                pixels=np.frombuffer(raw,dtype=dtype).astype(np.float32).reshape(h,w,4)
                if not np.isfinite(pixels).all(): raise ValueError(f'non-finite pixels: {p.name}')
                luma=pixels[:,:,:3]@np.array([.2126,.7152,.0722],np.float32)
                data.setdefault((s,r),[]).append((f,luma))
    if complete and (not continuity or n!=wanted or m.get('reason')!='complete'):raise ValueError('false complete sequence claim')
    for (s,r),entries in sorted(data.items()):
        stack=np.stack([e[1] for e in entries]);deltas=[]
        for i in range(1,len(entries)):
            if entries[i][0]==entries[i-1][0]+1:deltas.append(float(np.abs(stack[i]-stack[i-1]).mean()))
        metrics.append(dict(stage=m['stages'][s],region=r,frames=[e[0] for e in entries],
            mean_luma=[float(v.mean()) for v in stack],mean_pixel_temporal_std=float(stack.std(axis=0).mean()),
            consecutive_frame_mean_abs_delta=deltas))
    hashes['manifest.json']=hashlib.sha256(manifest.read_bytes()).hexdigest().upper()
    result=dict(schema='d18-layer-analysis-v1',source=str(root),complete=complete,gpu_complete=True,
        reason=m['reason'],frames=n,requested_frames=wanted,sequence_continuity=continuity,
        raw_bytes=total,sha256=hashes,metrics=metrics,
        boundary='Cropped external stage outputs; no guide/history pixels; motion and exposure may change. Different stage domains and statistics alone do not establish visible flicker or root cause.')
    return result,files

def write_report(result,out):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    (out/'layer-summary.json').write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
    rows=['# D18 两遍分阶段抓取', '',f"完整：{result['complete']}；帧数：{result['frames']}/{result['requested_frames']}；原因：{result['reason']}",
          '', 'GPU 完成已由生成端围栏确认；像素长度、有限性与哈希已核对。', '',
          '| 阶段 | 区域 | 像素时序标准差均值 |', '|---|---:|---:|']
    rows += [f"| {v['stage']} | {v['region']} | {v['mean_pixel_temporal_std']:.7g} |" for v in result['metrics']]
    rows += ['', '区域中的运动、曝光变化和正常历史收敛都会影响统计；不能仅凭数值判定闪烁。模型输出与 SR/最终画面颜色域不同，不直接跨域相减。缺失阶段保留为不完整证据。']
    (out/'layer-summary.md').write_text('\n'.join(rows)+'\n',encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory');p.add_argument('--output',required=True);a=p.parse_args()
    result,_=inspect(a.directory);write_report(result,a.output)
    print(json.dumps({k:result[k] for k in ('complete','reason','frames','raw_bytes')}))
