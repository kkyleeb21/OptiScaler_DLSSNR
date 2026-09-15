"""Disassemble only captured shader blobs referenced by binding samples. Local, bounded."""
import argparse,ctypes as c,json,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log');p.add_argument('archive');p.add_argument('--output',required=True);a=p.parse_args()
log=Path(a.log);archive=Path(a.archive)
if log.stat().st_size>128*1024*1024 or archive.stat().st_size>64*1024*1024:raise SystemExit('capture exceeds budget')
events=[json.loads(x) for x in log.read_text(encoding='utf-8-sig').splitlines()]
blobs={e['hash']:e for e in events if e['event']=='input_shader_blob' and e.get('complete')}
selected={e['shader_hash'] for e in events if e['event']=='input_binding' and e.get('shader_observed_at_create')}
selected.update(u['hash'] for e in events if e['event']=='input_binding' for u in e.get('upstream',[]) if u.get('known'))
if len(selected)>8192:raise SystemExit('too many shader identities')
data=archive.read_bytes();out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
dll=c.WinDLL('d3dcompiler_47.dll',winmode=0x800);dis=dll.D3DDisassemble
dis.argtypes=[c.c_void_p,c.c_size_t,c.c_uint,c.c_char_p,c.POINTER(c.c_void_p)];dis.restype=c.c_long
rows=[]
for key in sorted(selected):
 row=dict(hash=key,status='missing_blob');e=blobs.get(key)
 if e:
  start=e['offset'];size=e['bytes']
  if not(0<=start<=len(data) and 0<size<=256*1024 and start+size<=len(data)):raise ValueError('invalid blob range')
  code=data[start:start+size];h=14695981039346656037
  for byte in code:h=((h^byte)*1099511628211)&0xffffffffffffffff
  if f'{h:016x}'!=key:raise ValueError('blob hash mismatch')
  buf=c.create_string_buffer(code);blob=c.c_void_p();hr=dis(buf,size,0,None,c.byref(blob))
  row.update(bytes=size,sha256=hashlib.sha256(code).hexdigest(),hresult=hr,status='disassemble_failed')
  if hr>=0 and blob:
   vt=c.cast(blob,c.POINTER(c.POINTER(c.c_void_p))).contents
   ptr=c.WINFUNCTYPE(c.c_void_p,c.c_void_p)(vt[3])(blob);length=c.WINFUNCTYPE(c.c_size_t,c.c_void_p)(vt[4])(blob)
   text=c.string_at(ptr,length).rstrip(b'\0').decode('utf-8');c.WINFUNCTYPE(c.c_ulong,c.c_void_p)(vt[2])(blob)
   (out/(key+'.asm')).write_text(text,encoding='utf-8');row.update(status='disassembled',declarations=[line for line in text.splitlines() if line.startswith('dcl_')])
 rows.append(row)
(out/'index.json').write_text(json.dumps(rows,indent=2));print('Selected:',len(selected),'Disassembled:',sum(r['status']=='disassembled' for r in rows))
