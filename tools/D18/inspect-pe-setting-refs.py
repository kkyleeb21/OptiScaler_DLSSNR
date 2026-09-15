"""Read-only, bounded PE string/xref inventory for native setting adapters.
RIP-relative LEA/MOV byte matches are candidates, not verified instructions/setters.
"""
import argparse,bisect,hashlib,json,re,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('image');p.add_argument('--term',action='append',required=True)
p.add_argument('--output',required=True)
p.add_argument('--target-va',action='append',default=[],help='Additional numeric VA for direct call/jump candidate inventory')
p.add_argument('--data-va',action='append',default=[],help='Additional numeric VA for RIP-relative candidate inventory')
p.add_argument('--unwind-va',action='append',default=[],help='Resolve a VA through the x64 exception table and chained unwind records')
a=p.parse_args();path=Path(a.image);out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
if path.stat().st_size>512*1024*1024 or len(a.term)>16:raise SystemExit('Budget exceeded')
b=path.read_bytes()
u16=lambda o:struct.unpack_from('<H',b,o)[0]
u32=lambda o:struct.unpack_from('<I',b,o)[0]
nt=u32(0x3c)
if b[:2]!=b'MZ' or b[nt:nt+4]!=b'PE\0\0' or u16(nt+24)!=0x20b:raise SystemExit('Expected PE32+')
base=struct.unpack_from('<Q',b,nt+24+24)[0]
secstart=nt+24+u16(nt+20);sections=[]
for i in range(u16(nt+6)):
 o=secstart+i*40
 sections.append(dict(name=b[o:o+8].rstrip(b'\0').decode('ascii','replace'),rva=u32(o+12),virtual_size=u32(o+8),offset=u32(o+20),size=u32(o+16),flags=u32(o+36)))
def va(offset):
 for s in sections:
  if s['offset']<=offset<s['offset']+s['size']:return base+s['rva']+offset-s['offset']
 return None
strings=[]
for term in a.term:
 for encoding in ('ascii','utf-16-le'):
  q=term.encode(encoding);start=0
  for _ in range(16):
   at=b.find(q,start)
   if at<0:break
   strings.append(dict(term=term,encoding=encoding,offset=at,va=va(at)));start=at+len(q)
targets={s['va'] for s in strings if s['va']}
targets.update(int(v,0) for v in a.data_va)
refs=[]
for s in sections:
 if not s['flags']&0x20000000:continue
 data=b[s['offset']:s['offset']+s['size']]
 for m in re.finditer(rb'[\x48\x4c][\x8d\x8b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....',data,re.DOTALL):
  address=base+s['rva']+m.start();dest=address+7+struct.unpack_from('<i',m.group(),3)[0]
  if dest in targets and len(refs)<128:refs.append(dict(va=address,target=dest,bytes=m.group().hex(),kind='unverified_rip_candidate'))
branches=[]
branch_targets={int(v,0) for v in a.target_va}
for s in sections:
 if not s['flags']&0x20000000 or not branch_targets:continue
 data=b[s['offset']:s['offset']+s['size']]
 for m in re.finditer(rb'[\xe8\xe9]....',data,re.DOTALL):
  address=base+s['rva']+m.start();dest=address+5+struct.unpack_from('<i',m.group(),1)[0]
  if dest in branch_targets and len(branches)<128:branches.append(dict(va=address,target=dest,bytes=m.group().hex(),kind='unverified_branch_candidate'))
pointers=[]
for dest in targets:
 for m in re.finditer(re.escape(struct.pack('<Q',dest)),b):
  if len(pointers)>=128:break
  pointers.append(dict(va=va(m.start()),offset=m.start(),target=dest))
unwind=[]
if len(a.unwind_va)>32:raise SystemExit('Unwind query budget exceeded')
if a.unwind_va:
 def raw(rva,size):
  for s in sections:
   delta=rva-s['rva']
   if 0<=delta and delta+size<=s['size']:
    offset=s['offset']+delta
    if offset+size<=len(b):return offset
  raise ValueError('Unwind RVA is outside file-backed sections')
 er,es=struct.unpack_from('<II',b,nt+24+112+3*8)
 if es>32*1024*1024 or es%12:raise SystemExit('Invalid exception directory size')
 eo=raw(er,es)
 entries=[struct.unpack_from('<III',b,o) for o in range(eo,eo+es,12)]
 for query in a.unwind_va:
  address=int(query,0);rva=address-base
  matches=[row for row in entries if row[0]<=rva<row[1]]
  result=dict(va=address,matches=len(matches),chain=[],primary_entry=None)
  if len(matches)==1:
   row=matches[0];seen=set()
   for _ in range(32):
    if row in seen:raise ValueError('Unwind chain cycle')
    seen.add(row);o=raw(row[2],4)
    version,prolog,count,frame=struct.unpack_from('<BBBB',b,o);flags=version>>3
    if version&7 not in (1,2):raise ValueError('Unsupported unwind version')
    result['chain'].append(dict(begin=base+row[0],end=base+row[1],unwind_rva=row[2],flags=flags,prolog=prolog,code_count=count))
    if not flags&4:
     result['primary_entry']=base+row[0];break
    tail=row[2]+4+((count+1)&~1)*2
    row=struct.unpack_from('<III',b,raw(tail,12))
   else:raise ValueError('Unwind chain exceeds budget')
  unwind.append(result)
record=dict(schema='d18-pe-setting-candidates-v1',image=str(path),sha256=hashlib.sha256(b).hexdigest(),image_base=base,sections=sections,strings=strings,rip_candidates=refs,branch_candidates=branches,absolute_pointers=pointers,unwind_queries=unwind,boundary='Static byte matches and unwind ownership only; no live setter identity, calling convention, thread affinity or runtime safety established')
(out/'inventory.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
print(json.dumps(dict(strings=strings,rip_candidates=refs,branch_candidates=branches,absolute_pointers=pointers,unwind_queries=unwind),indent=2))
