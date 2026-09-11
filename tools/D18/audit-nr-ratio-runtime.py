"""Read-only PE string-xref/function audit; does not load or patch the DLL."""
import sys, struct, hashlib, json
from pathlib import Path
sys.path.insert(0, r'E:\DLSSNR\workspace\dlss5\vendor\capstone_runtime')
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
p=Path(sys.argv[1]); out=Path(sys.argv[2]); out.mkdir(parents=True,exist_ok=True)
b=p.read_bytes(); u16=lambda x:struct.unpack_from('<H',b,x)[0];u32=lambda x:struct.unpack_from('<I',b,x)[0]
pe=u32(0x3c); opt=pe+24; base=struct.unpack_from('<Q',b,opt+24)[0]
sections=[]
for i in range(u16(pe+6)):
 s=opt+u16(pe+20)+i*40
 sections.append((b[s:s+8].rstrip(b'\0').decode(),u32(s+12),u32(s+16),u32(s+20)))
def off(r):
 for _,va,size,raw in sections:
  if va<=r<va+size:return raw+r-va
 raise ValueError(hex(r))
def rva(o):
 for _,va,size,raw in sections:
  if raw<=o<raw+size:return va+o-raw
 raise ValueError(hex(o))
def cstr(o):return b[o:b.index(0,o)].decode(errors='replace')
ex=off(u32(opt+112)); funcs=off(u32(ex+28)); names=off(u32(ex+32)); ords=off(u32(ex+36))
exports={cstr(off(u32(names+i*4))):hex(u32(funcs+u16(ords+i*2)*4)) for i in range(u32(ex+24))}
pd=off(u32(opt+112+3*8)); plen=u32(opt+116+3*8)
runtimefuncs=[(u32(i),u32(i+4)) for i in range(pd,pd+plen,12)]
strings=[];pos=0
while True:
 pos=b.find(b'DLSSNR.ScalingRatio\0',pos)
 if pos<0:break
 strings.append(rva(pos));pos+=1
refs=[]
for name,va,size,raw in sections:
 if name!='.text':continue
 for i in range(raw,raw+size-7):
  if b[i] in (0x48,0x4c) and b[i+1]==0x8d and b[i+2]&0xc7==5:
   at=va+i-raw;target=at+7+struct.unpack_from('<i',b,i+3)[0]
   if target in strings:refs.append(at)
md=Cs(CS_ARCH_X86,CS_MODE_64)
requested=refs+[int(x,0) for x in sys.argv[3:]]
selected=sorted(set(next(((s,e) for s,e in runtimefuncs if s<=r<e),(r-32,r+128)) for r in requested))
for s,e in selected:
 lines=[f'{i.address-base:08X} {i.bytes.hex():24} {i.mnemonic:8} {i.op_str}' for i in md.disasm(b[off(s):off(e-1)+1],base+s)]
 (out/f'function-{s:x}.asm').write_text('\n'.join(lines))
result={'path':str(p),'sha256':hashlib.sha256(b).hexdigest(),'strings':[hex(x) for x in strings],'xrefs':[hex(x) for x in refs],'functions':[(hex(s),hex(e)) for s,e in selected],'exports':exports}
(out/'index.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='exports'},indent=2))
