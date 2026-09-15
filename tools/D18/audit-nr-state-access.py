"""Read-only bounded member-offset candidates in PE unwind ranges, not a data-flow proof."""
from pathlib import Path
import argparse,hashlib,json,sys
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools/D24'));from ngx_audit import PE
sys.path.insert(0,str(root/'workspace/dlss5/vendor/capstone_runtime'))
from capstone import Cs,CS_ARCH_X86,CS_MODE_64,CS_OP_MEM
p=argparse.ArgumentParser();p.add_argument('runtime',type=Path);p.add_argument('--offset',type=lambda x:int(x,0),required=True);p.add_argument('--with-offset',type=lambda x:int(x,0));p.add_argument('--output',type=Path,required=True);a=p.parse_args()
assert not a.output.exists() and a.runtime.stat().st_size<=512*1024*1024
b=a.runtime.read_bytes();pe=PE(b);pd,size=pe.directory(3);assert size<=12*100000
md=Cs(CS_ARCH_X86,CS_MODE_64);md.detail=True;results=[];skipped=0
for start,end in sorted(set(pe.runpack('<III',pd+i)[:2] for i in range(0,size,12))):
 if not 0<end-start<=262144:skipped+=1;continue
 hits=[];companion=False
 for ins in md.disasm(pe.rread(start,end-start),pe.base+start):
  for op in ins.operands:
   if op.type!=CS_OP_MEM or ins.reg_name(op.mem.base) in ['rip','rsp','rbp']:continue
   if op.mem.disp==a.with_offset:companion=True
   if op.mem.disp==a.offset:hits.append(dict(rva=hex(ins.address-pe.base),assembly=ins.mnemonic+' '+ins.op_str,width=op.size,access=op.access))
 if hits and (a.with_offset is None or companion):results.append(dict(start=hex(start),end=hex(end),hits=hits))
assert len(results)<=2048
j=dict(schema='d18-nr-state-access-v1',runtime_sha256=hashlib.sha256(b).hexdigest().upper(),offset=hex(a.offset),with_offset=hex(a.with_offset) if a.with_offset is not None else None,ranges=results,skipped_large_ranges=skipped,boundary='Member displacement candidates only; register/object aliasing unproven. Unwind fragments are not full functions; leaf functions without unwind records may be absent. No code execution or mutation.')
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(j,indent=2),encoding='utf-8');print(json.dumps(j,indent=2))
