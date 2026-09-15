"""Bounded file-only DXBC/string inventory; candidates never imply live bindings."""
import argparse, hashlib, json, re, struct
from pathlib import Path

KEY = re.compile(r'depth|velocity|motion|jitter|temporal|reproject|exposure|gbuffer', re.I)
def scan(path):
    path=Path(path)
    if path.stat().st_size>512*1024*1024: return dict(path=str(path),skipped='size_limit')
    data=path.read_bytes(); shaders=[]; invalid=0; truncated=False
    for match in re.finditer(b'DXBC',data):
        pos=match.start()
        if len(shaders)>=2048: truncated=True;break
        try:
            size,count=struct.unpack_from('<II',data,pos+24)
            if not (32+4*count<=size<=16*1024*1024 and 0<count<=64 and pos+size<=len(data)):raise ValueError()
            chunks=[];names=[]
            for i in range(count):
                off=struct.unpack_from('<I',data,pos+32+4*i)[0]
                if off<32+4*count or off+8>size:raise ValueError()
                tag=data[pos+off:pos+off+4].decode('ascii')
                length=struct.unpack_from('<I',data,pos+off+4)[0]
                if off+8+length>size:raise ValueError()
                chunks.append(tag)
                if tag=='RDEF':
                    part=data[pos+off+8:pos+off+8+length]
                    names=[m.group().decode('ascii') for m in re.finditer(rb'[ -~]{4,200}',part) if KEY.search(m.group().decode('ascii'))][:80]
            shaders.append(dict(offset=pos,size=size,chunks=chunks,candidate_names=names))
        except (ValueError,struct.error,UnicodeError):invalid+=1
    candidates=[]
    for encoding,pattern in [('ascii',rb'[ -~]{5,240}'),('utf-16le',rb'(?:[ -~]\x00){5,240}')]:
        for match in re.finditer(pattern,data):
            value=match.group().decode(encoding)
            if KEY.search(value):
                candidates.append(dict(offset=match.start(),encoding=encoding,text=value))
                if len(candidates)>=400:break
        if len(candidates)>=400:break
    return dict(path=str(path),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),shaders=shaders,invalid_dxbc_candidates=invalid,shader_scan_truncated=truncated,string_candidates=candidates,string_limit_reached=len(candidates)>=400,live_binding='not_observed')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('files',nargs='+');a=p.parse_args()
    result=dict(schema='d18-shader-input-inventory-v1',files=[scan(f) for f in a.files],limitations='File offsets and name matches are not runtime addresses or confirmed resource roles. Packed archives and dynamic shaders are not decoded.')
    out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    for f in result['files']:print(f['path'],len(f.get('shaders',[])),'DXBC',len(f.get('string_candidates',[])),'strings')

