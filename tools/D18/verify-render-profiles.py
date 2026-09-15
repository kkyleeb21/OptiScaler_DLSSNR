"""Static PE identity verification. Does not load a DLL, runtime, or GPU API."""
from pathlib import Path
import argparse,struct,json,hashlib
def u16(b,o):return struct.unpack_from('<H',b,o)[0]
def u32(b,o):return struct.unpack_from('<I',b,o)[0]
def z(b,o):return b[o:b.index(0,o)].decode('ascii')
def exports(b):
 pe=u32(b,60);assert b[pe:pe+4]==b'PE\0\0' and u16(b,pe+4)==0x8664
 op=pe+24;assert u16(b,op)==0x20b
 sec=op+u16(b,pe+20)
 sections=[(u32(b,sec+i*40+12),max(u32(b,sec+i*40+8),u32(b,sec+i*40+16)),u32(b,sec+i*40+20)) for i in range(u16(b,pe+6))]
 def off(rva):
  for va,size,raw in sections:
   if va<=rva<va+size:return raw+rva-va
  raise ValueError('RVA outside sections')
 e=off(u32(b,op+112));func=off(u32(b,e+28));names=off(u32(b,e+32));ords=off(u32(b,e+36))
 result={z(b,off(u32(b,names+4*i))):u32(b,func+4*u16(b,ords+2*i)) for i in range(u32(b,e+24))}
 return result,off
def pdb_identity(path):
 b=path.read_bytes();assert b.startswith(b'Microsoft C/C++ MSF 7.00')
 block=u32(b,32);size=u32(b,44);mapblock=u32(b,52)
 blocks=[u32(b,mapblock*block+i*4) for i in range((size+block-1)//block)]
 directory=b''.join(b[i*block:(i+1)*block] for i in blocks)[:size]
 n=u32(directory,0);sizes=[u32(directory,4+4*i) for i in range(n)];cursor=4+4*n
 for i,s in enumerate(sizes):
  count=0 if s==0xffffffff else (s+block-1)//block
  pages=[u32(directory,cursor+4*k) for k in range(count)];cursor+=4*count
  if i==1:
   stream=b''.join(b[k*block:(k+1)*block] for k in pages)[:s];return stream[12:28],u32(stream,8)
 raise ValueError('PDB info stream missing')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('candidate',type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--profile-suffix',default='render-track candidate',choices=['render-track candidate','integrated candidate']);a=p.parse_args();rows=[]
 source=(a.candidate/'core-source/OptiScaler/dlssnr/BuildProfile.h').read_text(encoding='utf-8-sig')
 release_cap=9 if 'Capabilities = Diagnostic ? 0x0fu : 0x09u' in source else 1
 for name,cap in [('release',release_cap),('diagnostic',15)]:
  path=a.candidate/'outputs'/name/'OptiScaler.dll';b=path.read_bytes();symbols,off=exports(b)
  r=symbols['D18GetBuildCapabilities'];code=b[off(r):off(r)+6]
  assert code==b'\xb8'+struct.pack('<I',cap)+b'\xc3',(name,code.hex())
  r=symbols['D18GetBuildProfileName'];code=b[off(r):off(r)+8]
  assert code[:3]==b'\x48\x8d\x05' and code[7]==0xc3,(name,code.hex())
  text=z(b,off(r+7+struct.unpack_from('<i',code,3)[0]));assert text==f'D18 {name} - {a.profile_suffix}'
  manifest=json.loads((path.parent/'build-profile.json').read_text());digest=hashlib.sha256(b).hexdigest().upper();assert digest==manifest['sha256'] and manifest['exit_code']==0
  op=u32(b,60)+24;debug=off(u32(b,op+112+6*8));count=u32(b,op+116+6*8)//28
  records=[u32(b,debug+i*28+24) for i in range(count) if u32(b,debug+i*28+12)==2];assert len(records)==1
  cv=records[0];assert b[cv:cv+4]==b'RSDS'
  pdb=path.with_suffix('.pdb');assert pdb_identity(pdb)==(b[cv+4:cv+20],u32(b,cv+20));assert z(b,cv+24)=='OptiScaler.pdb'
  rows.append(dict(profile=name,sha256=digest,capabilities=cap,exported_name=text,pdb_sha256=hashlib.sha256(pdb.read_bytes()).hexdigest().upper(),pdb_matches_guid_and_age=True,verification='static_x64_export_and_PDB_identity_no_DLL_loading'))
 assert rows[0]['sha256']!=rows[1]['sha256']
 a.output.write_text(json.dumps(dict(passed=True,profiles=rows,evidence='Build identity only; not runtime or gameplay verification'),indent=2),encoding='utf-8');print(json.dumps(rows,indent=2))
if __name__=='__main__':main()
