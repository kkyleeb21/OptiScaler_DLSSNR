"""Offline binding/bytecode/caller correlation. Candidate labels never imply input validity."""
import argparse,json,re,collections,struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('capture');p.add_argument('--log',default='D18InputProbe.jsonl');a=p.parse_args();root=Path(a.capture)
def bounded(path,limit):
 if path.stat().st_size>limit:raise ValueError(f'input exceeds budget: {path.name}')
 return path.read_bytes()
events=[json.loads(x) for x in bounded(root/a.log,128*1024*1024).decode('utf-8-sig').splitlines() if x.strip()]
bindings=[e for e in events if e['event']=='input_binding'];modules=json.loads((root/'modules.json').read_text(encoding='utf-8-sig'))
index={r['hash']:r for r in json.loads((root/'disassembly/index.json').read_text())}
def caller(address):
 v=int(address,16)
 for m in modules:
  if m['base']<=v<m['base']+m['ModuleMemorySize']:return m['ModuleName']+'+0x'+format(v-m['base'],'x')
 return 'unresolved:'+address
groups=collections.defaultdict(list)
for b in bindings:groups[b['shader_hash']].append(b)
rows=[]
for h,bs in groups.items():
 decl=index.get(h,{}).get('declarations',[])
 outputs={int(m.group(1)) for d in decl if (m:=re.match(r'dcl_output o(\d+)\b',d))}
 slots={int(m.group(1)) for d in decl if (m:=re.search(r'\bt(\d+)\b',d))}
 textures={(t['slot'],t.get('id'),t.get('width'),t.get('height'),t.get('format'),t.get('view_format')) for b in bs for t in b['textures'] if t.get('present') and (t['slot'] in slots or h=='0000000000000000')}
 targets={(t['slot'],t.get('id'),t.get('width'),t.get('height'),t.get('format')) for b in bs for t in b['render_targets'] if t.get('present')}
 row=dict(hash=h,count=len(bs),stages=dict(collections.Counter(b['stage'] for b in bs)),contexts=dict(collections.Counter(b['context_type'] for b in bs)),callers=sorted({caller(b['caller']) for b in bs}),bytes=index.get(h,{}).get('bytes'),declarations=decl,declared_output_slots=sorted(outputs),used_textures=sorted(textures,key=str),targets=sorted(targets,key=str),samples=[b['sample'] for b in bs])
 rows.append(row)
out=root/'analysis.json';out.write_text(json.dumps(rows,indent=2))
# Resource identity correlations only: no proof of execution, contents, lifetime,
# view subresource overlap, or shader branch execution. Keep sweeps separate.
by_hash={r['hash']:r for r in rows};writers=collections.defaultdict(list);edges=[]
def resource_key(t):
 if 'resource_uid' in t:return ('uid',t['resource_uid']) if t['resource_uid'] else None
 return ('pointer',t['id']) if t.get('id') else None
for b in bindings:
 if b['shader_hash'] not in index:continue
 r=by_hash[b['shader_hash']]
 output_slots=r['declared_output_slots'] if b['stage']=='PS' else {int(m.group(1)) for d in index[b['shader_hash']]['declarations'] if (m:=re.search(r'\bu(\d+)\b',d))}
 for t in b['render_targets'] if b['stage']=='PS' else b.get('compute_uavs',[]):
  key=resource_key(t)
  if key and t['slot'] in output_slots:
   writers[(b['session'],b['sweep'],key)].append((b,t))
for b in bindings:
 if b['shader_hash'] not in index:continue
 slots={int(m.group(1)) for d in index[b['shader_hash']]['declarations'] if (m:=re.search(r'\bt(\d+)\b',d))}
 for t in b['textures']:
  key=resource_key(t)
  if not key or t['slot'] not in slots:continue
  for w,rt in writers.get((b['session'],b['sweep'],key),[]):
   if w['sample']>=b['sample']:continue
   edges.append(dict(session=b['session'],sweep=b['sweep'],resource=key,writer_sample=w['sample'],reader_sample=b['sample'],writer_hash=w['shader_hash'],reader_hash=b['shader_hash'],writer_stage=w['stage'],output_slot=rt['slot'],input_slot=t['slot'],writer_view=rt.get('view_desc_words'),reader_view=t.get('view_desc_words'),both_immediate=w['context_type']==b['context_type']==0))
archive=bounded(root/'D18InputProbe.shaders.bin',64*1024*1024);profiles=collections.Counter()
for e in events:
 if e['event']!='input_shader_blob' or not e.get('complete'):continue
 start,size=e['offset'],e['bytes']
 if not(0<=start and 32<=size<=256*1024 and start+size<=len(archive)):raise ValueError('invalid archive bounds')
 code=archive[start:start+size]
 if code[:4]!=b'DXBC':raise ValueError('invalid DXBC header')
 count=struct.unpack_from('<I',code,28)[0]
 if 32+4*count>len(code):raise ValueError('invalid chunk table')
 for i in range(count):
  offset=struct.unpack_from('<I',code,32+4*i)[0]
  if offset+12>len(code):raise ValueError('invalid chunk offset')
  if code[offset:offset+4] in (b'SHDR',b'SHEX'):
   version=struct.unpack_from('<I',code,offset+8)[0];stage=version>>16
   profiles[('PS','VS','GS','HS','DS','CS')[stage] if stage<6 else str(stage)]+=1
missing=[b for b in bindings if not b['shader_observed_at_create']]
summary=dict(complete_capture=False,archive_distinct_profiles=dict(profiles),missing_by_stage=dict(collections.Counter(b['stage'] for b in missing)),resource_identity_edges=edges,edge_limitations='Recorded order within a sweep; not execution or contents proof. V2 UIDs prevent pointer reuse aliases; view words retained but subresource overlap not validated. V1 lacks these fields and CS UAV IDs.')
(root/'resource-correlation.json').write_text(json.dumps(summary,indent=2))
print('Archive profiles:',dict(profiles),'identity edges:',len(edges))
print('Creation bytes:',sum(e['bytes'] for e in events if e['event']=='input_shader_identity'))
print('Missing tags by stage/context/caller:',dict(collections.Counter((b['stage'],b['context_type'],caller(b['caller'])) for b in bindings if not b['shader_observed_at_create'])))
for r in rows:
 if r['hash']=='0000000000000000':continue
 print(r['hash'],r['bytes'],r['count'],r['stages'],'slots',sorted({t[0] for t in r['used_textures']}),'screen textures',[(t[0],t[2],t[3],t[4]) for t in r['used_textures'] if t[2]==3840], 'targets',sorted({(t[0],t[2],t[3],t[4]) for t in r['targets']}))
