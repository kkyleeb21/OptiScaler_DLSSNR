"""Join bounded static DXBC inventory to runtime fingerprints; no game DLL loading."""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'D24'))
from ngx_audit import PE
p=argparse.ArgumentParser();p.add_argument('inventory');p.add_argument('log');p.add_argument('--output',required=True);a=p.parse_args()
inventory=json.loads(Path(a.inventory).read_text());events=[json.loads(x) for x in Path(a.log).read_text().splitlines()]
wanted={e.get('hash',e.get('shader_hash')) for e in events if e['event'] in ('input_shader_identity','input_binding')}
found=[]
for f in inventory['files']:
 path=Path(f['path'])
 if path.stat().st_size>512*1024*1024:continue
 data=path.read_bytes();pe=PE(data);labels={}
 for e in pe.exports():
  try:
   target=pe.runpack('<Q',e['rva'])[0]-pe.base;off=pe.offset(target)
   labels.setdefault(off,[]).extend(e['names'])
  except (ValueError,IndexError,OverflowError):pass
 for s in f.get('shaders',[])[:2048]:
  if s['size']>256*1024:continue
  h=14695981039346656037
  for b in data[s['offset']:s['offset']+s['size']]:h=((h^b)*1099511628211)&0xffffffffffffffff
  key=f'{h:016x}'
  if key in wanted:found.append(dict(hash=key,file=str(path),offset=s['offset'],bytes=s['size'],exports=labels.get(s['offset'],[])))
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(found,indent=2))
print('matched static shaders:',len(found))
for row in found:
 if row['hash'] in {e.get('shader_hash') for e in events if e['event']=='input_binding'}:print(json.dumps(row))
