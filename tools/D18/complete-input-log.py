"""Keep complete JSONL records from a live snapshot, preserving the raw source."""
import argparse,json,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('output');a=p.parse_args()
source=Path(a.source);out=Path(a.output)
if source.resolve()==out.resolve():raise SystemExit('raw source must be preserved')
if source.stat().st_size>128*1024*1024:raise SystemExit('log exceeds budget')
data=source.read_bytes();lines=data.splitlines(keepends=True);discarded=b'';records=0
for i,line in enumerate(lines):
 try:json.loads(line);records+=1
 except json.JSONDecodeError:
  if i!=len(lines)-1 or line.endswith(b'\n'):raise
  discarded=line
complete=data[:-len(discarded)] if discarded else data
out.write_bytes(complete)
audit=dict(raw_sha256=hashlib.sha256(data).hexdigest(),complete_sha256=hashlib.sha256(complete).hexdigest(),records=records,discarded_tail_bytes=len(discarded),discarded_tail=discarded.decode('utf-8',errors='replace'),reason='Incomplete trailing record of a live-file snapshot' if discarded else 'All records complete')
out.with_suffix('.integrity.json').write_text(json.dumps(audit,indent=2));print(json.dumps(audit))
