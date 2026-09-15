"""Bounded DX11 native NR evidence; counters are observations, not session totals."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

def read(path):
    rows, invalid = [], 0
    for line in path.read_text(encoding='utf-8-sig', errors='replace').splitlines():
        try: rows.append(json.loads(line))
        except json.JSONDecodeError: invalid += 1
    return rows, invalid

def summarize(path, execution=None):
    rows, invalid=read(path)
    trace, trace_invalid=read(execution) if execution and execution.exists() else ([],0)
    calls=[r for r in trace if r.get('stage')=='native_nr_return']
    bad={'skip_or_failure','exception_circuit_breaker','release_exception','evaluate_failed','completion_failed','runtime_hash_rejected','runtime_already_loaded'}
    resources=sorted({(r.get('role'),r.get('width'),r.get('height'),r.get('format'),r.get('bind')) for r in rows if r.get('event')=='dx11_resource'})
    rendered=[r.get('value') for r in rows if r.get('event')=='rendered_frames']
    advanced=[r for r in rows if r.get('event')=='native_advanced']
    mismatch=[r for r in rows if (r.get('event')=='skip_or_failure' and r.get('value')==-30)
              or (r.get('event')=='native_advanced' and r.get('result')==-30)]
    findings=[]
    if mismatch:
        findings.append(dict(code='shared_parameters_mismatch',category='configuration_fallback',
                             observations=len(mismatch),
                             explanation='Shared history rejected unequal settings across active passes; this is not evidence of a GPU execution failure.',
                             action='Copy the previous pass into each additional active pass, or select independent history.',
                             boundary='Bounded observations do not establish the current state, number of attempts, or recovery.'))
    if not advanced:
        findings.append(dict(code='missing_advanced_frame_observations',category='evidence_gap',
                             explanation='No per-frame requested/ready/recorded pass observations; instance creation alone does not prove multipass completion.'))
    return dict(schema='d18-native-nr-summary-v1',sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                records=len(rows),invalid_lines=invalid,execution_invalid_lines=trace_invalid,
                events=dict(Counter(r.get('event','unlabelled') for r in rows)),
                completed_model_frame_observations=rendered,
                advanced_frame_observations=advanced,
                findings=findings,
                observation_coverage=dict(advanced_frames=bool(advanced),model_completion=bool(rendered),bridge_returns=bool(calls)),
                bridge_success_records=sum(r.get('code')==0 and r.get('b')==1 for r in calls),
                errors=[r for r in rows if r.get('event') in bad]+[r for r in calls if r.get('code',0)!=0],
                resource_contracts=[dict(role=a,width=b,height=c,format=d,bind=e) for a,b,c,d,e in resources],
                boundary='Bounded native model completion and bridge return observations. Bridge success precedes the producer colour-handoff fence; counters may reset and must not be summed into session totals. Missing/stale logs do not prove current execution or visual correctness.')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--execution',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.write_text(json.dumps(summarize(a.log,a.execution),indent=2),encoding='utf-8')
