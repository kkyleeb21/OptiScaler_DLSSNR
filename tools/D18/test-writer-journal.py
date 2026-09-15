import json,runpy
from pathlib import Path
summary=runpy.run_path(str(Path(__file__).with_name('summarize-writer-journal.py')))['summarize']
def row(n,frame,kind,d=0,s=0,invalid=False):
 return dict(event='dx11_writer',sequence=n,frame=frame,kind=kind,destination=dict(id=d),source=dict(id=s),invalid=invalid,shader='0',slot=0)
def run(rows):return summary(('\n'.join(json.dumps(r) for r in rows)).encode())
result=run([row(1,1,'clear_uav_uint',d=7),row(2,1,'draw_source',s=7,invalid=True),row(3,2,'draw_source',s=7)])
assert result['observed_after_invalidation']==1
assert len(result['clears'][0]['following_same_resource'])==1
assert run([])['records']==0
assert summary(b'{"event":')['incomplete_tail']
assert run([row(n,1,'draw_target',d=1) for n in range(1,257)])['at_limit']
for rows in ([row(2,1,'draw_target')],[row(n,1,'draw_target') for n in range(1,258)]):
 try:run(rows)
 except ValueError:pass
 else:raise AssertionError('Invalid stream accepted')
print('writer summary: same-frame identity, post-invalidation records, empty/bounded/malformed streams passed')
