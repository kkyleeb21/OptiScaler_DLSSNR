"""Trace bounded, same-frame resource bindings; never infer pixel correctness."""
import argparse, collections, hashlib, json, re
from pathlib import Path

def trace(bindings, declarations, seed_hash, seed_slot):
    groups=collections.defaultdict(list)
    for b in bindings:
        if b.get('event')=='input_binding' and b.get('context_type')==0:
            groups[tuple(b.get(k) for k in ('session','sweep','frame','device_id','context_id'))].append(b)
    paths=[]
    for key, rows in groups.items():
        if any(v is None for v in key):continue
        tracked={}
        for b in sorted(rows,key=lambda x:x['sample']):
            decl=declarations.get(b['shader_hash'],[])
            reads={int(m.group(1)) for d in decl if (m:=re.search(r'\bt(\d+)\b',d))}
            writes={int(m.group(1)) for d in decl if (m:=re.search(r'dcl_output o(\d+)\b' if b['stage']=='PS' else r'\bu(\d+)\b',d))}
            outputs=b.get('render_targets',[]) if b['stage']=='PS' else b.get('compute_uavs',[])
            sources=[t for t in b.get('textures',[]) if t.get('present') and t['slot'] in reads and t.get('resource_uid') in tracked]
            for out in outputs:
                uid=out.get('resource_uid')
                if not out.get('present') or not uid or out['slot'] not in writes:continue
                base=dict(group=list(key),sample=b['sample'],shader=b['shader_hash'],output_slot=out['slot'],
                          output_uid=uid,output_size=[out.get('width'),out.get('height')],viewport=b.get('viewport'))
                if b['shader_hash']==seed_hash and out['slot']==seed_slot:
                    tracked[uid]=[b['sample']];paths.append(dict(base,kind='seed'))
                    continue
                # Snapshot ancestors before replacing this binding's output identity.
                candidates=[]
                for src in sources:
                    parent=tracked.get(src['resource_uid'],[])
                    if not parent or len(parent)>=16 or b['sample'] in parent:continue
                    iw,ih=src.get('width'),src.get('height');ow,oh=out.get('width'),out.get('height')
                    change='unknown'
                    if all(isinstance(v,int) and v>0 for v in (iw,ih,ow,oh)):
                        change='same' if (iw,ih)==(ow,oh) else 'larger' if ow>=iw and oh>=ih else 'smaller' if ow<=iw and oh<=ih else 'mixed'
                    candidates.append(parent+[b['sample']])
                    paths.append(dict(base,kind='candidate',input_slot=src['slot'],input_uid=src['resource_uid'],
                                      input_size=[iw,ih],size_change=change,ancestor_samples=parent+[b['sample']],
                                      input_view=src.get('view_desc_words'),output_view=out.get('view_desc_words')))
                if candidates:tracked[uid]=min(candidates,key=len)
                else:tracked.pop(uid,None) # A different recorded writer invalidates the old lineage.
                if len(paths)>10000:raise ValueError('Resolution path budget exceeded')
    return dict(schema='d18-resolution-chain-v1',seed_hash=seed_hash,seed_slot=seed_slot,paths=paths,
                counts=dict(collections.Counter(p.get('size_change',p['kind']) for p in paths)),
                boundary='Same-frame immediate-context observed bindings and declared slots only. Texture allocation sizes, not validated view extents. No proof of shader branch execution, full frame coverage, pixel dependency, final presentation, or a safe replacement point.')

def main():
    p=argparse.ArgumentParser();p.add_argument('log');p.add_argument('--index',required=True);p.add_argument('--seed-hash',required=True);p.add_argument('--seed-slot',type=int,default=1);p.add_argument('--output',required=True);a=p.parse_args()
    raw=Path(a.log).read_bytes()
    if len(raw)>128*1024*1024:raise ValueError('Log exceeds budget')
    rows=[json.loads(x) for x in raw.decode('utf-8-sig').splitlines() if x.strip()]
    index_path=Path(a.index)
    if index_path.stat().st_size>16*1024*1024:raise ValueError('Index exceeds budget')
    index={x['hash']:x.get('declarations',[]) for x in json.loads(index_path.read_text(encoding='utf-8-sig'))}
    result=trace(rows,index,a.seed_hash,a.seed_slot);result['log_sha256']=hashlib.sha256(raw).hexdigest()
    Path(a.output).write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result['counts']))

if __name__=='__main__':main()
