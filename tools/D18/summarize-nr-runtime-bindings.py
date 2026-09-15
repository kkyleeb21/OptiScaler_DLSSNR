"""Read-only phase adapter for pinned owned-host binding/resource observations.

Opaque backend handles are compared within a run; never dereferenced or equated
across processes. Pixel controls require an identical host/runtime/driver.
"""
from pathlib import Path
import argparse, hashlib, json, re

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest().upper()

def summarize(run, reference):
    m=json.loads((run/'manifest.json').read_text(encoding='utf-8'))
    trace=run/'runtime-state.jsonl'
    assert m['complete'] and m['observer_complete'] and trace.stat().st_size<=1024*1024
    assert sha(trace)==m['observer_trace_sha256']
    records=[json.loads(v) for v in trace.read_text(encoding='utf-8').splitlines()]
    start=records[0]
    assert start['schema']=='d18-nr-state-observer-v3'
    phase=m['observer_phase']
    assert phase in ['bindings','resources'] and start.get(phase+'_phase')==1
    assert start['code_modified'] is False
    assert records[-1]['event']=='exit' and records[-1]['code']==0
    assert not any(r['event'] in ['exception','observer_failed'] for r in records)
    points={k:[r for r in records if r.get('point')==k] for k in range(4)}
    for rows in points.values():
        assert [r['call'] for r in rows]==list(range(128))
    assert [(r['call'],r['point']) for r in records if r['event']=='hit']==[(i,k) for i in range(128) for k in range(4)]
    log=(run/'probe.log').read_text(encoding='utf-8')
    requested=re.findall(r'BOUNDARY frame=(\d+) pass=(\d+) feature=(\d+) params=(\d+) requested_reset=(\d+) frozen=(\d+)',log)
    assert len(requested)==128 and 'debug_layer=1' in log
    for i,r in enumerate(requested):
        assert tuple(map(int,r[:2]))==divmod(i,2)
    expected_features=2 if m['mode']=='feedback_live_independent' else 1
    assert m['mode'] in ['feedback_live_shared','feedback_live_independent'] and m['reset_policy']=='initial'
    assert len({r['feature'] for r in points[0]})==expected_features
    row=dict(run=str(run.resolve()),phase=phase,calls=128,active_features=expected_features,
             trace_sha256=sha(trace),manifest_sha256=sha(run/'manifest.json'),observer_sha256=m['observer_sha256'])
    if phase=='bindings':
        for i,(entry,gate,launch) in enumerate(zip(points[1],points[2],points[3])):
            expected_counter=i//expected_features
            assert entry['counter']==gate['counter']==launch['argument_counter']==expected_counter
            assert entry['state']==gate['state']
            assert gate['gated_inputs']==[entry['inputs'][0]]+([0,0,0] if expected_counter==0 else entry['inputs'][1:])
            assert launch['argument_words'][:4]==gate['gated_inputs']
            if 'block_inputs' in entry:
                assert entry['layer_index']==0 and entry['inputs']==entry['block_inputs']
                assert entry['input_layers']==[] and entry['input_blocks']==[0,1,2,3]
                assert all(entry[k]==[] for k in ['completion_layers','completion_outputs','completion_blocks'])
        row['counter_per_pass']=[[r['counter'] for r in points[1][p::2]] for p in range(2)]
        row['input_nonzero_unique_counts']=[len({r['inputs'][k] for r in points[1] if r['inputs'][k]}) for k in range(4)]
        row['direct_block_input_mapping_observed']=all('block_inputs' in r for r in points[1])
        row['dispatch_target_rvas']=sorted({hex(r['dispatch_target_rva']) for r in points[3]})
    else:
        histories={}
        copies=0
        for i,(entry,setter,after) in enumerate(zip(points[1],points[2],points[3])):
            feature=entry['feature']
            assert feature==points[0][i]['feature']==after['feature']
            histories.setdefault(feature,entry['history_resource'])
            assert entry['history_resource']==histories[feature]==after['history_resource']
            initial=i<expected_features
            assert bool(entry['history_valid'])==(not initial) and bool(entry['reset'])==initial
            assert bool(setter['history_handle'])==(not initial) and bool(setter['mv_handle'])==(not initial)
            assert setter['optional_handle']==0 and setter['input_gate']==1
            assert after['copy_from_temporary']==1
            if 'source_resource' in after:
                assert after['source_resource']==after['selected_output_resource']
                assert after['destination_resource']==entry['history_resource']
                assert after['source_resource']!=after['destination_resource']
                copies+=1
        assert len(set(histories.values()))==expected_features
        row.update(history_resource_count=len(set(histories.values())),actual_copy_call_observations=copies,
                   copy_direction='internal temporary output to per-feature history color' if copies==128 else 'copy flag only; call not observed',
                   history_resources_per_pass=[len({r['history_resource'] for r in points[1][p::2]}) for p in range(2)],
                   mv_resources_per_pass=[len({r['resources'][1] for r in points[1][p::2]}) for p in range(2)])
    ref=json.loads((reference/'manifest.json').read_text(encoding='utf-8'))
    assert ref['complete']
    for key in ['runtime_sha256','driver_core_sha256','executable_sha256','mode','reset_policy','ratio','style','frozen_input_sha256','width','height','passes','frames_evaluated','fixed_input_resource']:
        assert m[key]==ref[key],key
    assert set(m['files'])==set(ref['files']) and len(m['files'])==34
    for name,h in m['files'].items():
        assert Path(name).name==name and sha(run/name)==h and sha(reference/name)==ref['files'][name]
        assert h==ref['files'][name],name+' pixels differ from unobserved control'
    row.update(reference=str(reference.resolve()),exact_files=34,host_sha256=m['executable_sha256'])
    return row

def main():
    p=argparse.ArgumentParser()
    p.add_argument('runs',nargs='+',type=Path)
    p.add_argument('--reference',action='append',type=Path,required=True)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args()
    assert len(a.runs)==len(a.reference) and not a.output.exists()
    rows=[summarize(run,ref) for run,ref in zip(a.runs,a.reference)]
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(dict(schema='d18-nr-runtime-bindings-summary-v1',runs=rows,boundary='Pinned runtime, owned host, main thread only. Static provenance plus call arguments do not prove all hidden temporal state or game correctness. Copy calls are CPU submission observations; completed host fences/readbacks separately establish GPU completion for this fixture.'),indent=2),encoding='utf-8')
    for r in rows:
        print(Path(r['run']).name,r['phase'],'calls',r['calls'],'exact files',r['exact_files'],'history copies',r.get('actual_copy_call_observations'))

if __name__=='__main__':
    main()
