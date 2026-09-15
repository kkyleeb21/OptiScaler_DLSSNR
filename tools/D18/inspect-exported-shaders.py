"""Resolve exported shader-data pointers from PE bytes; never load the game DLL."""
import argparse,ctypes as c,json,sys,hashlib,re
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'D24'))
from ngx_audit import PE
p=argparse.ArgumentParser();p.add_argument('file');p.add_argument('--output',required=True);p.add_argument('--pattern',required=True);a=p.parse_args()
pe=PE(Path(a.file).read_bytes());out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
dll=c.WinDLL('d3dcompiler_47.dll',winmode=0x800)
dis=dll.D3DDisassemble;dis.argtypes=[c.c_void_p,c.c_size_t,c.c_uint,c.c_char_p,c.POINTER(c.c_void_p)];dis.restype=c.c_long
rows=[]
for entry in pe.exports():
 for name in entry['names']:
  if not re.fullmatch(a.pattern,name):continue
  if len(rows)>=32:raise RuntimeError('selection exceeds 32 shader limit')
  row=dict(export=name,export_rva=entry['rva'],status='unresolved',live_binding='not_observed')
  try:
   rva=pe.runpack('<Q',entry['rva'])[0]-pe.base
   header=pe.rread(rva,32)
   if header[:4]!=b'DXBC':raise ValueError('export is not a pointer to DXBC')
   size=int.from_bytes(header[24:28],'little')
   if not 32<=size<=16*1024*1024:raise ValueError('size limit')
   data=pe.rread(rva,size);buf=c.create_string_buffer(data);blob=c.c_void_p()
   hr=dis(buf,size,0,None,c.byref(blob))
   if hr<0:raise ValueError('D3DDisassemble HRESULT '+str(hr))
   vt=c.cast(blob,c.POINTER(c.POINTER(c.c_void_p))).contents
   ptr=c.WINFUNCTYPE(c.c_void_p,c.c_void_p)(vt[3])(blob)
   length=c.WINFUNCTYPE(c.c_size_t,c.c_void_p)(vt[4])(blob)
   text=c.string_at(ptr,length).rstrip(b'\0').decode('utf-8')
   c.WINFUNCTYPE(c.c_ulong,c.c_void_p)(vt[2])(blob)
   (out/(name+'.asm')).write_text(text,encoding='utf-8')
   row.update(status='disassembled',shader_rva=rva,file_offset=pe.offset(rva),sha256=hashlib.sha256(data).hexdigest(),declarations=[s for s in text.splitlines() if s.startswith('dcl_')])
  except (ValueError,IndexError) as e:row['reason']=str(e)
  rows.append(row)
(out/'exports.json').write_text(json.dumps(rows,indent=2),encoding='utf-8');print(json.dumps(rows,indent=2))
