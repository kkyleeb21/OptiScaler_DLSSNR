"""Verify explicit one-field counter interventions; never label them observation-only."""
from pathlib import Path
import argparse,hashlib,json,re

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()

def checked_manifest(run):
    m=json.loads((run/'manifest.json').read_text(encoding='utf-8'))
    assert m['complete'] and m['exit_code']==0 and len(m['files'])==34
    for n,h in m['files'].items():
        assert Path(n).name==n and sha(run/n)==h
    return m

def summarize(run,baseline,independent):
    m=checked_manifest(run);base=checked_manifest(baseline);indie=checked_manifest(independent)
    assert m['runtime_data_intervention'] and m['observer_complete'] and m['mode']=='feedback_live_shared'
    assert base['mode']=='feedback_live_shared' and indie['mode']=='feedback_live_independent'
    for ref in [base,indie]:
        for key in ['runtime_sha256','driver_core_sha256','executable_sha256','ratio','style','width','height','passes','frames_evaluated','reset_policy','frozen_input_sha256']:
            assert m[key]==ref[key]
    trace=run/'runtime-state.jsonl'
    assert trace.stat().st_size<=1024*1024 and sha(trace)==m['observer_trace_sha256']
    records=[json.loads(v) for v in trace.read_text(encoding='utf-8').splitlines()]
    assert records[0]['schema']=='d18-nr-counter-intervention-v1' and records[0]['data_modified'] is True and records[0]['code_modified'] is False
    assert records[-1]['event']=='exit' and records[-1]['code']==0
    assert not any(r['event'] in ['exception','observer_failed'] for r in records)
    points={k:[r for r in records if r.get('point')==k] for k in range(4)}
    assert [(r['call'],r['point']) for r in records if r['event']=='hit']==[(i,k) for i in range(128) for k in range(4)]
    policy=m.get('counter_policy','per_pass')
    assert policy in ['per_pass','second_cold_start']
    assert records[0].get('counter_policy',policy)==policy
    assert len({r['feature'] for r in points[0]})==len({r['state'] for r in points[1]})==1
    assert sum(bool(r['reset_before_control']) for r in points[0])==1
    assert len({tuple(r['controls_bits']) for r in points[0]})==1
    before=[];after=[]
    for i,(entry,gate,launch) in enumerate(zip(points[1],points[2],points[3])):
        expected_before=0 if i==0 else after[-1]+1
        expected_after=i//2 if policy=='per_pass' else (0 if i==1 else expected_before)
        assert entry['counter_before_intervention']==expected_before
        assert entry['counter_after_intervention']==entry['counter']==gate['counter']==launch['argument_counter']==expected_after
        assert gate['gated_inputs']==[entry['inputs'][0]]+([0,0,0] if expected_after==0 else entry['inputs'][1:])
        assert launch['argument_words'][:4]==gate['gated_inputs']
        before.append(expected_before);after.append(expected_after)
    assert len({r['inputs'][1] for r in points[1] if r['inputs'][1]})==1
    log=(run/'probe.log').read_text(encoding='utf-8')
    assert 'debug_layer=1' in log and 'PASS real NR finite stage readbacks' in log
    requested=re.findall(r'BOUNDARY frame=(\d+) pass=(\d+) feature=(\d+) params=(\d+) requested_reset=(\d+) frozen=(\d+)',log)
    assert len(requested)==128 and sum(int(r[4]) for r in requested)==1
    return dict(run=str(run.resolve()),policy=policy,data_intervention=True,calls=128,writes=128,changed_values=sum(x!=y for x,y in zip(before,after)),counter_before=before,counter_after=after,one_nonzero_history_input_handle=True,
                baseline_exact_files=sum(h==base['files'][n] for n,h in m['files'].items()),independent_exact_files=sum(h==indie['files'][n] for n,h in m['files'].items()),compared_files=34,
                unique_output_hashes_per_pass=[len({m['files'][f'pass{s}_{f}.rgba16f'] for f in range(48,64)}) for s in [1,2]],
                manifest_sha256=sha(run/'manifest.json'),trace_sha256=sha(trace),observer_sha256=m['observer_sha256'])

def main():
    p=argparse.ArgumentParser();p.add_argument('runs',nargs='+',type=Path);p.add_argument('--baseline',required=True,type=Path);p.add_argument('--independent',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    assert not a.output.exists()
    rows=[summarize(r,a.baseline,a.independent) for r in a.runs]
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(dict(schema='d18-counter-isolation-summary-v1',runs=rows,boundary='Owned-host runtime DATA intervention, pinned private counter only. No resource-pointer, code or disk-runtime changes. Static synthetic GPU fixture; not a game fix or complete history isolation.'),indent=2),encoding='utf-8')
    for r in rows:print(r['policy'],'calls',r['calls'],'changed values',r['changed_values'],'unique outputs',r['unique_output_hashes_per_pass'])

if __name__=='__main__':main()
