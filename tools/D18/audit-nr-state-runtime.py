"""Bounded read-only NR key xrefs and function disassembly; never loads or patches a DLL."""
from pathlib import Path
import argparse,hashlib,json,struct,sys
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools/D24'));from ngx_audit import PE
sys.path.insert(0,str(root/'workspace/dlss5/vendor/capstone_runtime'))
from capstone import Cs,CS_ARCH_X86,CS_MODE_64,CS_OP_MEM
p=argparse.ArgumentParser();p.add_argument('runtime',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--key',action='append');p.add_argument('--function',type=lambda v:int(v,0),action='append',default=[]);p.add_argument('--call-target',type=lambda v:int(v,0),action='append',default=[]);a=p.parse_args()
assert not a.output.exists();a.output.mkdir(parents=True)
assert a.runtime.stat().st_size<=512*1024*1024,'Runtime exceeds static audit size limit'
b=a.runtime.read_bytes();pe=PE(b);keys=a.key or ['DLSSNR.Reset'];targets={}
assert len(keys)<=32 and all(0<len(k)<=256 for k in keys)
assert len(a.function)<=32 and len(a.call_target)<=32
for key in keys:
 needle=key.encode()+b'\0';at=0
 while True:
  at=b.find(needle,at)
  if at<0:break
  for sec in pe.sections:
   if sec['offset']<=at<sec['offset']+sec['size']:targets[sec['va']+at-sec['offset']]=key;break
  at+=1
refs=[]
for sec in pe.sections:
 if not sec['executable']:continue
 raw=sec['offset'];data=pe.read(raw,sec['size'])
 for i in range(len(data)-6):
  if data[i] in (0x48,0x4c) and data[i+1]==0x8d and data[i+2]&0xc7==5:
   rva=sec['va']+i;target=rva+7+struct.unpack_from('<i',data,i+3)[0]
   if target in targets:refs.append(dict(rva=rva,key=targets[target],string_rva=target))
 for i in range(len(data)-4):
  if data[i]==0xe8:
   rva=sec['va']+i;target=rva+5+struct.unpack_from('<i',data,i+1)[0]
   if target in a.call_target:refs.append(dict(rva=rva,call_target=target))
pd,size=pe.directory(3);functions=[pe.runpack('<III',pd+i)[:2] for i in range(0,size,12)]
selected={}
for rva in [r['rva'] for r in refs]+a.function:
 matches=[(s,e) for s,e in functions if s<=rva<e]
 if matches:selected[min(matches,key=lambda v:v[1]-v[0])]='unwind_range'
 else:selected[(rva,rva+128)]='fallback_128_bytes_may_include_adjacent_functions'
assert len(selected)<=32
md=Cs(CS_ARCH_X86,CS_MODE_64);md.detail=True;exports=pe.exports();artifacts=[];rip_data=[]
for start,end in sorted(selected):
 assert 0<end-start<=262144,'Function exceeds bounded audit'
 lines=[]
 for i in md.disasm(pe.rread(start,end-start),pe.base+start):
  lines.append(f'{i.address-pe.base:08X} {i.bytes.hex():24} {i.mnemonic:8} {i.op_str}')
  for op in i.operands:
   if op.type!=CS_OP_MEM or i.reg_name(op.mem.base)!='rip':continue
   rva=i.address+i.size+op.mem.disp-pe.base
   try:raw=pe.rread(rva,160)
   except (ValueError,IndexError,struct.error):continue
   entry=dict(instruction_rva=hex(i.address-pe.base),target_rva=hex(rva))
   string=raw.split(b'\0')[0]
   if len(string)<160 and string and all(32<=x<127 for x in string):entry['ascii']=string.decode('ascii')
   elif i.mnemonic=='movss':entry['f32_bits']=raw[:4].hex()
   else:continue
   rip_data.append(entry)
 file=a.output/f'function-{start:x}.asm';file.write_text('\n'.join(lines),encoding='utf-8');artifacts.append(dict(start=hex(start),end=hex(end),range_kind=selected[(start,end)],file=file.name,sha256=hashlib.sha256(file.read_bytes()).hexdigest().upper()))
result=dict(schema='d18-nr-state-static-v2',runtime=str(a.runtime.resolve()),runtime_sha256=hashlib.sha256(b).hexdigest().upper(),keys=keys,string_rvas={hex(k):v for k,v in targets.items()},candidate_xrefs=refs,functions=artifacts,rip_data=rip_data,exports=exports,boundary='Candidate RIP-relative LEA and direct E8 references require decoded inspection. Unwind ranges or explicit 128-byte fallback (may include adjacent functions); bounded ASCII/float-bit annotations are raw data, not semantic proof. Not complete call graph or API discovery; no binary modified or executed.')
(a.output/'index.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in result.items() if k!='exports'},indent=2))
