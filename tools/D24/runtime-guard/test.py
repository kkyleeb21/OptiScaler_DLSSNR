"""Compare shipping CLI/addon file admission, never load/execute the NR runtime."""
from pathlib import Path
import argparse,base64,ctypes,hashlib,json,subprocess
p=argparse.ArgumentParser(description=__doc__)
for name in ['addon','checker','runtime','output']:p.add_argument('--'+name,required=True,type=Path)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
original=a.runtime.read_bytes();identity=hashlib.sha256(original).hexdigest()
addon=ctypes.WinDLL(str(a.addon.resolve()));check=addon.D24CheckRuntime
check.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_uint)];check.restype=ctypes.c_int
manifest=json.loads((Path(__file__).resolve().parents[3]/'community/d18-installer/runtime_patch.json').read_text(encoding='utf-8-sig'))
rows=[];temporary=a.output/'synthetic-never-execute.dll'
def observe(label,path,accepted):
 cli=subprocess.run([str(a.checker.resolve()),str(path.resolve())],capture_output=True,text=True,timeout=30)
 result=json.loads(cli.stdout);offset=ctypes.c_uint();native=bool(check(str(path.resolve()),ctypes.byref(offset)))
 assert result['rule']=='d18-patch-sites-v1',result
 assert native==result['accepted']==accepted,(label,result,native)
 assert offset.value==result['offset'] and cli.returncode==(0 if accepted else 1),(label,result,offset.value)
 rows.append({'case':label,'accepted':accepted,'result':result,'native_parity':True})
 print(label,'PASS',flush=True)
def variant(label,data,accepted):
 temporary.write_bytes(data);observe(label,temporary,accepted)
spans=[];patched=bytearray(original)
for h in manifest['hunks']:
 offset=h['offset'];before=base64.b64decode(h['expected_base64']);after=base64.b64decode(h.get('replacement_base64',''.join(h.get('replacement_base64_parts',[]))))
 allowed=[before]+[base64.b64decode(x) for x in h.get('compatible_input_base64',[])]+[after]
 assert any((offset==len(patched) if not x else patched[offset:offset+len(x)]==x) for x in allowed),'Input patch conflict'
 patched[offset:offset+len(after)]=after
 spans.append((offset,offset+max(len(before),len(after))))
try:
 observe('original',a.runtime,True)
 variant('patched-copy',patched,True)
 outside=next(x for x in range(0x1000,0x2000) if all(not lo<=x<hi for lo,hi in spans))
 for label,base in [('original-outside-patch',original),('patched-outside-patch',patched)]:
  b=bytearray(base);b[outside]^=1;variant(label,b,True)
 b=bytearray(original);site=next(h['offset'] for h in manifest['hunks'] if base64.b64decode(h['expected_base64']));b[site]^=1;variant('patch-conflict',b,False)
 pe=int.from_bytes(original[60:64],'little');b=bytearray(original);b[pe+4:pe+6]=b'\x4c\x01';variant('wrong-architecture',b,False)
 b=bytearray(original);b[:2]=b'NO';variant('invalid-pe',b,False)
 variant('truncated',original[:128],False)
 optional=int.from_bytes(original[pe+20:pe+22],'little');table=pe+24+optional
 for i in range(int.from_bytes(original[pe+6:pe+8],'little')):
  off=table+40*i;rva=int.from_bytes(original[off+12:off+16],'little');size=int.from_bytes(original[off+16:off+20],'little');raw=int.from_bytes(original[off+20:off+24],'little')
  if rva<=0x20f2c and 0x20f2c+7<=rva+size:
   b=bytearray(original);b[raw+0x20f2c-rva]^=1;variant('native-instruction-conflict',b,False);break
 else:raise AssertionError('Missing native patch site')
 observe('missing-file',a.output/'not-present.dll',False)
finally:
 temporary.unlink(missing_ok=True)
assert hashlib.sha256(a.runtime.read_bytes()).hexdigest()==identity,'Original changed'
(a.output/'results.json').write_text(json.dumps({'scope':'host admission only; no NR execution or GPU; originals preserved','runtime_sha256':identity,'cases':rows},indent=2)+'\n')
