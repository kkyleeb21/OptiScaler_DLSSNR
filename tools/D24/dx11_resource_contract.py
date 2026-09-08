"""Hash-pinned, read-only resource-contract disassembly with annotated references."""
import hashlib,json,pathlib,sys,struct
from ngx_audit import PE
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'workspace/dlss5/tools/third_party/capstone'))
import capstone as c
from capstone.x86 import X86_OP_MEM,X86_REG_RIP
p=ROOT/'workspace/dlss5/extracted/DLSS310.8.0-Streamline2.13/nvngx_dlssnr.dll'
data=p.read_bytes(); sha=hashlib.sha256(data).hexdigest()
assert sha=='e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e'
pe=PE(data); md=c.Cs(c.CS_ARCH_X86,c.CS_MODE_64);md.detail=True
out=ROOT/'evidence/D24/dx11-resource-contract-20260907';out.mkdir(exist_ok=True)
ranges=[(0x1f570,0x20e60),(0x21880,0x23010),(0x570b0,0x578a0),(0x579b0,0x58550),(0x59f00,0x5a1d0),(0x5a660,0x5abe0),(0x5afe0,0x5b170),(0x19e70,0x1a180),(0x1c080,0x1d200)]
ranges += [(int(x,16),next((e for s,e in pe.runtime_functions() if s==int(x,16)),int(x,16)+128)) for x in sys.argv[1:]]
for start,end in ranges:
    lines=[]
    for i in md.disasm(pe.rread(start,end-start),pe.base+start):
        note=[]
        for o in i.operands:
            if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP:
                target=i.address+i.size+o.mem.disp-pe.base
                try:
                    raw=pe.rread(target,200).split(b'\0')[0];txt=raw.decode('utf-8')
                    if len(txt)>4 and txt.isprintable(): note.append(hex(target)+' '+txt)
                except (ValueError,UnicodeError):pass
        lines.append(f'{i.address-pe.base:08x} {i.mnemonic:8} {i.op_str}'+(' ; '+' | '.join(note) if note else ''))
    (out/f'{start:08x}.asm').write_text('\n'.join(lines),encoding='utf-8')
print(out)
