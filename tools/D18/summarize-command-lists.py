"""Bounded DX11 command-list metadata; never interprets it as GPU or visual success."""
import argparse
import collections
import hashlib
import json
from pathlib import Path

REASONS = {1: 'recording_prefix_missing', 2: 'bindings_unknown', 4: 'hook_coverage_missing',
           8: 'write_or_operation_budget', 16: 'unsupported_operation', 32: 'metadata_missing'}
DECISIONS = {0: 'disjoint', 1: 'unknown', 2: 'affects_chain', 3: 'observation_only'}
EVENTS = {'frame_begin', 'frame_end', 'execute', 'write', 'operation', 'present_before',
          'detail_policy', 'present_after', 'ui_begin', 'ui_end', 'sr_private_seed'}


def summarize(path):
    raw = Path(path).read_bytes()
    first = raw.split(b'\n', 1)[0]
    extended = len(first) < 4096 and json.loads(first or b'{}').get('schema') == 'd18-command-list-frame-v2'
    if len(raw) > (64 if extended else 12) * 1024 * 1024 + 4096:
        raise ValueError('Command-list journal exceeds file budget')
    lines = raw.splitlines()
    if len(lines) > (65536 if extended else 8192):
        raise ValueError('Command-list journal exceeds record budget')
    frames, issues, events = [], [], collections.Counter()
    frame = execution = None
    shader_state = {}; input_state = {}; pass_by_ordinal = {}
    tail = False
    for index, line in enumerate(lines):
        try:
            item = json.loads(line)
        except (ValueError, UnicodeDecodeError):
            if index == len(lines) - 1 and not raw.endswith(b'\n'):
                tail = True
                break
            raise ValueError(f'Malformed record {index + 1}') from None
        if not isinstance(item, dict) or item.get('event') not in EVENTS:
            raise ValueError(f'Unknown event at record {index + 1}')
        if item.get('sequence') != index + 1:
            raise ValueError(f'Sequence discontinuity at record {index + 1}')
        event = item['event']
        events[event] += 1
        if event == 'frame_begin':
            if item.get('schema') not in ('d18-command-list-frame-v1', 'd18-command-list-frame-v2'):
                raise ValueError('Unknown frame schema')
            frame = dict(frame=item['frame'], start=item, end=None, executions=[], phases=[])
            frames.append(frame)
            execution = None
        elif frame is None or frame['frame'] != item.get('frame'):
            raise ValueError(f'Event outside matching frame at record {index + 1}')
        elif event == 'frame_end':
            frame['end'] = item
            frame = execution = None
        elif event == 'execute':
            if item.get('decision') not in DECISIONS:
                raise ValueError('Unknown command-list decision')
            execution = dict(item, decision_name=DECISIONS[item['decision']], resources=[],
                             methods=collections.Counter(), operation_records=0, passes=[])
            shader_state = {}; input_state = {'PS': {}, 'CS': {}}; pass_by_ordinal = {}
            mask = item.get('reasons', 32)
            execution['reason_names'] = [name for bit, name in REASONS.items() if mask & bit]
            execution['unknown_reason_bits'] = mask & ~sum(REASONS)
            if item['decision'] == 0 and mask:
                issues.append(dict(sequence=item['sequence'], issue='disjoint_with_incomplete_metadata'))
            frame['executions'].append(execution)
        elif event == 'detail_policy':
            if execution is None or execution['list'] != item.get('list'):
                raise ValueError('Detail policy outside matching execution')
            execution['operations_selected'] = item['operations_selected']
        elif event in ('write', 'operation'):
            if execution is None or execution['list'] != item.get('list'):
                raise ValueError('Resource/operation outside matching execution')
            if event == 'write':
                execution['resources'].append(item['resource'])
            else:
                execution['operation_records'] += 1
                execution['methods'][str(item['method'])] += 1
                method = item['method']
                if method == 110:
                    shader_state = {}; input_state = {'PS': {}, 'CS': {}}
                elif method in (9, 69):
                    shader_state['PS' if method == 9 else 'CS'] = item['shader']
                elif method in (8, 67) and item['source']['view_dimension']:
                    input_state['PS' if method == 8 else 'CS'][item['binding']] = item['source']
                elif method in (8, 67) and not item['source']['id']:
                    # Setter envelope and null binding records share this shape.
                    # Never treat retained bindings as proven current reads.
                    input_state['PS' if method == 8 else 'CS'].pop(item['binding'], None)
                elif method in (12, 13, 20, 21, 38, 39, 40, 41, 42):
                    stage = 'CS' if method in (41, 42) else 'PS'
                    key = item['ordinal']
                    if key not in pass_by_ordinal:
                        entry = dict(ordinal=key, method=method, stage=stage,
                                     shader=shader_state.get(stage), args=item['args'],
                                     observed_inputs=dict(input_state[stage]), targets=[],
                                     binding_boundary='Shadow bindings only; may contain unused or automatically unbound resources')
                        pass_by_ordinal[key] = entry
                        execution['passes'].append(entry)
                    if item['args'][11] and item['destination']['id']:
                        pass_by_ordinal[key]['targets'].append(dict(slot=item['binding'], role=item['args'][11], resource=item['destination']))
        else:
            frame['phases'].append(item)
    for f in frames:
        f['envelope_complete'] = bool(f['end'] and f['end'].get('complete'))
        f['execution_details_complete'] = bool(f['executions']) and all(
            e['operation_records'] == e['trace_records'] and not e['trace_dropped']
            and len(e['resources']) == e['writes'] for e in f['executions'])
        selected = [e for e in f['executions'] if e.get('operations_selected', True)]
        f['selected_details_complete'] = (all(e['operation_records'] == e['trace_records']
            and not e['trace_dropped'] for e in selected) if selected else None)
        f['intentionally_omitted_lists'] = sum(e.get('operations_selected') is False for e in f['executions'])
        f['write_metadata_complete'] = bool(f['executions']) and all(
            e['reasons'] == 0 for e in f['executions'])
        before = [p for p in f['phases'] if p['event'] == 'present_before']
        after = [p for p in f['phases'] if p['event'] == 'present_after']
        f['present_pair_observed'] = bool(before and after)
        valid_buffers = bool(before and after and before[-1].get('buffer_status') == 0
            and after[-1].get('buffer_status') == 0 and before[-1].get('backbuffer', {}).get('id')
            and after[-1].get('backbuffer', {}).get('id'))
        f['same_backbuffer_identity'] = (before[-1]['backbuffer']['id'] ==
            after[-1]['backbuffer']['id']) if valid_buffers else None

    return dict(schema='d18-command-list-summary-v1', sha256=hashlib.sha256(raw).hexdigest(),
                records=sum(events.values()), partial_tail=tail, event_counts=events,
                validation_issues=issues, frames=frames,
                boundary='Selected command-list metadata and CPU phase ordering only. No complete '
                'immediate-context trace, GPU payload, full pipeline state, GPU completion or visual '
                'acceptance. Resource writes are a conservative superset; absent phases are unknown.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    result = summarize(args.journal)
    Path(args.output).write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(dict(frames=len(result['frames']), records=result['records'],
                         issues=result['validation_issues'], partial_tail=result['partial_tail'])))
    raise SystemExit(bool(result['validation_issues']))
