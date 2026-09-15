"""Summarize bounded CPU writer observations, never certify GPU or lineage correctness."""
import argparse,collections,json,hashlib
from pathlib import Path

def summarize(raw):
 if len(raw)>1024*1024:raise ValueError('Writer journal exceeds 1 MiB')
 lines=raw.decode('utf-8-sig').splitlines();rows=[];incomplete_tail=False
 for index,line in enumerate(lines):
  if not line.strip():continue
  try:rows.append(json.loads(line))
  except json.JSONDecodeError:
   if index==len(lines)-1 and not raw.endswith(b'\n'):incomplete_tail=True
   else:raise
 if len(rows)>256:raise ValueError('Writer journal exceeds 256 records')
 for i,e in enumerate(rows,1):
  if e.get('event')!='dx11_writer' or e.get('sequence')!=i:raise ValueError('Invalid writer event/sequence')
  for key in ('destination','source'):
   if not isinstance(e.get(key),dict) or 'id' not in e[key]:raise ValueError('Resource identity missing')
 clears=[]
 for i,e in enumerate(rows):
  if not e['kind'].startswith('clear_') or not e['destination']['id']:continue
  uid=e['destination']['id'];following=[]
  for later in rows[i+1:]:
   if later['frame']!=e['frame']:break
   if uid in (later['destination']['id'],later['source']['id']):
    following.append({k:later[k] for k in ('sequence','kind','shader','slot','invalid','destination','source')})
  clears.append(dict(clear=e,following_same_resource=following))
 return dict(schema='d18-dx11-writer-summary-v1',log_sha256=hashlib.sha256(raw).hexdigest(),records=len(rows),at_limit=len(rows)==256,incomplete_tail=incomplete_tail,
  selected_frames=sorted({e['frame'] for e in rows}),kinds=dict(collections.Counter(e['kind'] for e in rows)),
  observed_after_invalidation=sum(e['invalid'] for e in rows),clears=clears,
  boundary='Selected-frame CPU metadata only. Shared resource ID is not proof of overlapping view ranges, pixel dependency, complete API coverage or safe writeback. Unobserved frames/operations are unknown.')

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('log');p.add_argument('--output',required=True);a=p.parse_args()
 result=summarize(Path(a.log).read_bytes());Path(a.output).write_text(json.dumps(result,indent=2),encoding='utf-8')
 print(json.dumps({k:result[k] for k in ('records','at_limit','selected_frames','kinds','observed_after_invalidation')}))
