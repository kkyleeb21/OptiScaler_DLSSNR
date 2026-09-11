"""Read-only stock-specific backend inventory; never loads or modifies the DLL."""
import hashlib, json, pathlib, re, struct, sys
from ngx_audit import PE
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'workspace/dlss5/tools/third_party/capstone'))
import capstone as cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
src = ROOT / 'workspace/dlss5/extracted/DLSS310.8.0-Streamline2.13/nvngx_dlssnr.dll'
data = src.read_bytes()
sha = hashlib.sha256(data).hexdigest()
assert sha == 'e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e'
out = ROOT / 'evidence/D24/dx11-backend-hunt-20260907'
out.mkdir(exist_ok=True)
pe = PE(data)
md = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_64); md.detail = True; md.skipdata = True
instructions = []
refs = []
for s in pe.sections:
    if not s['executable']: continue
    for ins in md.disasm(pe.rread(s['va'], s['size']), pe.base+s['va']):
        if not ins.id: continue
        rva = ins.address-pe.base
        instructions.append((rva, ins.mnemonic, ins.op_str))
        for op in ins.operands:
            target = None
            if op.type == X86_OP_IMM and (ins.mnemonic == 'call' or ins.mnemonic.startswith('j')):
                target = op.imm-pe.base
            elif op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                target = ins.address+ins.size+op.mem.disp-pe.base
            if target is not None: refs.append(dict(at=rva, target=target, asm=ins.mnemonic+' '+ins.op_str))
strings = []
for s in pe.sections:
    for m in re.finditer(rb'[\x20-\x7e]{6,}', pe.rread(s['va'], s['size'])):
        val = m.group().decode('ascii')
        if re.search(r'd3d11|dx11|d3d12|vulkan|cuda|\.\?AV|\.\?AU', val, re.I):
            strings.append(dict(rva=s['va']+m.start(), text=val))
targets = {0x16860,0x19e70,0x14480,0x4e740,0x77a20,0x12b30}
selected = [x for x in refs if x['target'] in targets]
def dump(start, end):
    (out/f'{start:08x}.asm').write_text('\n'.join(f'{a:08x} {m:8} {o}' for a,m,o in instructions if start<=a<end), encoding='utf-8')
dump(0x11000,0x13b20); dump(0x14480,0x14760); dump(0x77a20,0x77c00)
for name, obj in [('strings', strings), ('references', refs), ('anchors',selected)]:
    (out/f'{name}.json').write_text(json.dumps(obj,indent=2),encoding='utf-8')
(out/'manifest.json').write_text(json.dumps(dict(source=str(src),sha256=sha,method='Linear executable section disassembly and ASCII inventory; candidates, not whole-program reachability',instructions=len(instructions),refs=len(refs)),indent=2),encoding='utf-8')
print(json.dumps(selected,indent=2))
print('TYPE AND DX11 STRINGS')
for s in strings:
    if re.search(r'd3d11|dx11|\.\?AV|\.\?AU',s['text'],re.I): print(hex(s['rva']),s['text'][:220])
assert hashlib.sha256(src.read_bytes()).hexdigest()==sha
backend = {}
for name, type_rva in [('D3D11',0x1142018),('D3D12',0x11420a8),('Vulkan',0x1141f58),('CUDA',0x1142490)]:
    cols = []
    for s in pe.sections:
        if s['executable']: continue
        raw = pe.rread(s['va'],s['size'])
        for m in re.finditer(re.escape(struct.pack('<I',type_rva)),raw):
            col = s['va']+m.start()-12
            try: fields = pe.runpack('<6I',col)
            except ValueError: continue
            if fields[0]!=1 or fields[5]!=col: continue
            for t in pe.sections:
                if t['executable']: continue
                buf = pe.rread(t['va'],t['size'])
                for p in re.finditer(re.escape(struct.pack('<Q',pe.base+col)),buf):
                    vt=t['va']+p.start()+8
                    slots=[]
                    for slot in range(0,0x200,8):
                        fn=pe.runpack('<Q',vt+slot)[0]-pe.base
                        if not any(z['executable'] and z['va']<=fn<z['va']+z['size'] for z in pe.sections): break
                        slots.append(dict(slot=hex(slot),function=hex(fn)))
                    cols.append(dict(col=hex(col),vtable=hex(vt),offset=fields[1],slots=slots,references=[r for r in refs if r['target']==vt]))
    backend[name]=cols
(out/'backend-vtables.json').write_text(json.dumps(backend,indent=2),encoding='utf-8')
print('BACKEND VTABLES',json.dumps(backend['D3D11'],indent=2))
dxstrings={x['rva']:x['text'] for x in strings if 'D3D11' in x['text'] or 'not yet supported' in x['text']}
dxrefs=[dict(**r,text=dxstrings[r['target']]) for r in refs if r['target'] in dxstrings]
(out/'dx11-string-xrefs.json').write_text(json.dumps(dxrefs,indent=2),encoding='utf-8')
print('DX11 STRING XREFS',json.dumps(dxrefs,indent=2))
dump(0x54000,0x5e000)
for start,end in [(0x7650,0x7720),(0x20e60,0x2187b),(0x59910,0x59919),(0x3120,0x3240),(0x56c90,0x56dd0),(0x59b00,0x59eb0),(0x58570,0x58710),(0x578a0,0x57b90),(0x570b0,0x57180),(0x19e70,0x1a110)]:
    (out/f'focused-{start:08x}.asm').write_text('\n'.join(f'{i.address-pe.base:08x} {i.mnemonic:8} {i.op_str}' for i in md.disasm(pe.rread(start,end-start),pe.base+start)),encoding='utf-8')
print('FACTORY XREFS',json.dumps([r for r in refs if r['target'] in [0x56c90,0x7650,0x76b0,0x20e80]],indent=2))
blocker = pe.rread(0xb01c0,0x80).split(b'\0')[0].decode('utf-8')
(out/'nr-dx11-blocker.json').write_text(json.dumps(dict(rva='0xb01c0',text=blocker,network_create='0x20e60',network_callers=[r for r in refs if r['target']==0x20e60],api_kind_slot='0x50',dx11_api_kind_method='0x59910'),indent=2),encoding='utf-8')
assert pe.runpack('<Q',0xb55f8+0x50)[0]==pe.base+0x59910
assert pe.runpack('<Q',0xb55f8+0x140)[0]==pe.base+0x58570
assert hashlib.sha256(src.read_bytes()).hexdigest()==sha
