"""Join the bounded CS archive to actual writer observations, reuse the shared disassembler."""
from pathlib import Path
import argparse,json,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('directory');a=p.parse_args();root=Path(a.directory)
index=root/'D18PostCompute.jsonl';archive=root/'D18PostCompute.bin';journal=root/'D18PostReplay.jsonl'
if index.stat().st_size>256*1024 or archive.stat().st_size>16*1024*1024 or journal.stat().st_size>1024*1024:raise SystemExit('Compute capture exceeds budget')
def rows(p):
 raw=p.read_bytes();lines=raw.decode('utf-8-sig').splitlines();out=[]
 for i,line in enumerate(lines):
  if not line.strip():continue
  try:out.append(json.loads(line))
  except json.JSONDecodeError:
   if i!=len(lines)-1 or raw.endswith(b'\n'):raise
 return out
blobs=rows(index);events=rows(journal)
if len(blobs)>256 or len(events)>256:raise SystemExit('Compute record cap exceeded')
selected={e['shader'] for e in events if (e['kind']=='dispatch' or e['kind'].startswith('compute_')) and e['shader']!='0000000000000000'}
derived=[dict(event='input_shader_blob',hash=e['hash'],offset=e['offset'],bytes=e['bytes'],complete=e['complete']) for e in blobs]
derived += [dict(event='input_binding',shader_hash=h,shader_observed_at_create=True) for h in sorted(selected)]
path=root/'compute-observed.jsonl';path.write_text('\n'.join(json.dumps(e) for e in derived)+'\n',encoding='utf-8')
subprocess.run([sys.executable,str(Path(__file__).with_name('disassemble-observed-shaders.py')),str(path),str(archive),'--output',str(root/'compute-disassembly')],check=True)
result=dict(schema='d18-compute-observations-v1',selected=sorted(selected),captured=len(blobs),unknown_dispatches=sum(e['kind']=='dispatch' and e['shader']=='0000000000000000' for e in events),
 legacy_declaration_dispatches=sum(e['kind']=='dispatch' and (len(e.get('dispatch_details',[]))<12 or e['dispatch_details'][11]!=2) for e in events),
 declaration_boundary='Only source marker 2 denotes executable declarations. Legacy reflection-empty masks may be wrong for stripped shaders and must not establish unused bindings.',
 boundary='Static declarations joined to selected CPU dispatch observations; no proof of actual pixel writes or safe replay. Missing blobs remain missing.')
(root/'compute-summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
