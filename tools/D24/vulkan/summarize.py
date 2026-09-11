"""Summarize sampled Vulkan observations; recording is not GPU completion."""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path


def summarize(path):
    data = path.read_bytes()
    events = []
    for line in data.decode('utf-8-sig', errors='replace').splitlines():
        record = dict(token.split('=', 1) for token in line.split() if '=' in token)
        if record.get('event'):
            events.append(record)
    counts = collections.Counter(e['event'] for e in events)
    frames = [e for e in events if e['event'] == 'nr_frame']
    modes = collections.Counter(e.get('mode', 'unknown') for e in frames)
    model = sum(e.get('mode') == '2' and e.get('result') == '1' for e in frames)
    conversion = modes['1']
    submissions = [e for e in events if e['event'] == 'sr_submit' and e.get('result') == '0']
    completed = {e.get('epoch') for e in events if e['event'] == 'gpu_completed' and e.get('epoch')}
    correlated = [e for e in submissions if e.get('epoch') in completed]
    barriers = [e for e in events if e['event'] == 'last_barrier']
    timing = []
    for event in events:
        if event['event'] != 'nr_timing' or event.get('scope') != 'encode_model_resolve' or event.get('nonblocking') != '1':
            continue
        try:
            ms = float(event.get('ms', 'nan'))
        except ValueError:
            continue
        if math.isfinite(ms) and 0 <= ms < 1000:
            timing.append(ms)
    gaps = []
    if not frames:
        gaps.append('No sampled conversion or NR recording observed; logging may be disabled.')
    if modes['unknown']:
        gaps.append('Legacy frame events omit mode; model execution cannot be inferred.')
    if not barriers:
        gaps.append('No image layout observations in this log.')
    elif any(e.get('layout', 'unknown') == 'unknown' for e in barriers):
        gaps.append('Some sampled image layouts are unknown.')
    if not submissions:
        gaps.append('No successful sampled SR submission observed.')
    if not correlated:
        gaps.append('No sampled SR submission correlated with GPU completion by epoch.')
    return {
        'source': str(path.resolve()), 'source_sha256': hashlib.sha256(data).hexdigest(),
        'adapter': 'vulkan-text', 'events': dict(counts), 'modes': dict(modes),
        'sr_observed': counts['sr_snapshot'] > 0,
        'native_execution': 'model_recorded' if model else 'conversion_only_recorded' if conversion else 'not_observed',
        'evidence': {'conversion_recordings': conversion, 'model_recordings': model,
                     'unknown_mode_recordings': modes['unknown'],
                     'successful_sr_submissions': len(submissions),
                     'sr_submissions_with_completion': len(correlated),
                     'nr_gpu_completion': 'not_correlated'},
        'device_results': [e for e in events if e['event'] == 'device_result'],
        'allocation_failures': [e for e in events if e['event'] == 'allocation_failed'],
        'parameter_roundtrips': [e for e in events if e['event'] == 'nr_parameter'],
        'nr_gpu_timing': {'scope': 'encode_model_resolve', 'unit': 'ms', 'samples': len(timing),
                          'min': min(timing) if timing else None, 'max': max(timing) if timing else None,
                          'latest': timing[-1] if timing else None, 'pc_latency': False},
        'nr_events': [e for e in events if e['event'].startswith('nr_')],
        'required_extensions': [e for e in events if e['event'] == 'required_extension'],
        'feature_requests': [e for e in events if e['event'] == 'device_request'],
        'resource_snapshots': [e for e in events if e['event'] in ('sr_snapshot', 'resource', 'feature_flags', 'float', 'uint')],
        'submission_and_layout_evidence': [e for e in events if e['event'] in
            ('recording', 'last_barrier', 'sr_submit', 'completion_fence', 'gpu_completed', 'retirement_ready', 'device_tracking_destroy')],
        'unresolved': gaps, 'gameplay_verdict': 'not_measured',
        'limitations': 'Sampled metadata only. Layout observations are partial. SR epochs are not linked to nr_frame records; SR completion does not prove NR completion or visual correctness.',
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.log)
    args.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({'native_execution': result['native_execution'], 'report': str(args.out)}))
