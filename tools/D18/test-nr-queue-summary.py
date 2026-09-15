import importlib.util
import pathlib
import sys
p=pathlib.Path(__file__).parent/'summarize-diagnostics.py'
spec=importlib.util.spec_from_file_location('summary',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
reasons=['queue_unknown_list','queue_ambiguous','queue_history_stale','queue_clock_invalid',
         'queue_device_mismatch','queue_observer_unavailable','queue_sr_list_limit','queue_list_not_direct']
assert all(len(r)<29 for r in reasons)
raw=[]
def event(kind,reason,result,flags):
 raw.append(m.RECORD.pack(len(raw)+1,1000,1,0,17,23,0,0,3840,2160,0,0,0,0,result,flags,1,1,1,0,0,kind.encode(),reason.encode(),1))
event('nr_queue_history','promoted_pinned_execute',1,0)
for reason in reasons:event('nr_outcome',reason+';n=2;s=1000001;h=0',0,1<<12)
header=m.HEADER.pack(b'D18DIAG',1,m.HEADER.size,m.RECORD.size,len(raw),1,len(raw),0,0,b'observed',b'DX12',b'queue-fixture')
ring=pathlib.Path(sys.argv[1])/'queue-summary.ring';ring.write_bytes(header+b''.join(raw))
h,r=m.read_ring(ring);s=m.summarize(h,r)
assert len(s['nr_queue_history'])==1 and s['nr_queue_history'][0]['queue']==17
assert s['nr_outcome_summary']['fallback_reasons']=={reason:2 for reason in reasons}
assert not s['pending_recordings'] and not s['incomplete_fences']
assert s['first_anomaly']['sequence']==2
assert 'promotions observed: 1' in m.markdown(s)
assert not m.summarize(h,[])['nr_queue_history']
print('PASS C4 queue promotion and rejection subreasons -> schema 1 -> shared summary; no false GPU completion')
