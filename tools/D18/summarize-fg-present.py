"""Summarize bounded d18_fg_present metadata emitted on the present thread."""
import argparse
import json
import re
from collections import Counter
from pathlib import Path


def summarize_vulkan_contract(text):
    rows = []
    later_barriers = []
    camera_policies = []
    sampled_inputs = []
    sampled_guides = []
    malformed = 0
    numeric = ('record', 'epoch', 'submitted', 'same_queue', 'submit_failed', 'invalidated',
               'handoffs', 'later_barriers', 'other_cmd_barriers', 'swapchains', 'waits', 'result')
    for line in text.splitlines():
        if 'event=fg_sampled_input ' in line or 'event=fg_sampled_guide ' in line:
            fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
            try:
                required = ('record', 'handoffs', 'submitted', 'same_queue', 'invalidated', 'failed',
                            'descriptor_writes', 'sr_barriers', 'later_barriers', 'other_commands', 'result') \
                    if fields['event'] == 'fg_sampled_input' else \
                    ('record', 'alive', 'metadata', 'sampled_layout', 'present_layout', 'layout_known',
                     'ownership_conflict', 'aspect', 'format', 'usage', 'width', 'height')
                for key in required:
                    fields[key] = int(fields[key])
                if fields['event'] == 'fg_sampled_guide' and fields.get('role') not in ('depth', 'motion'):
                    raise ValueError('unknown guide role')
                (sampled_inputs if fields['event'] == 'fg_sampled_input' else sampled_guides).append(fields)
            except (KeyError, ValueError):
                malformed += 1
            continue
        if 'event=fg_input_later_barrier ' in line:
            fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
            try:
                row = {key: int(fields[key]) for key in ('record', 'epoch', 'old_layout', 'new_layout',
                       'aspect', 'source_family', 'destination_family', 'source_access', 'destination_access')}
                row['role'] = fields['role']; row['coverage'] = fields['coverage']
                later_barriers.append(row)
            except (KeyError, ValueError):
                malformed += 1
            continue
        if 'event=fg_camera_policy ' in line:
            fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
            try:
                row = {key: int(fields[key]) for key in ('valid', 'flags_known', 'approximate')}
                row.update({key: float(fields[key]) for key in ('near', 'far', 'vfov_rad', 'aspect')})
                row.update({key: fields[key] for key in ('planes_source', 'fov_source')})
                camera_policies.append(row)
            except (KeyError, ValueError):
                malformed += 1
            continue
        if 'event=fg_input_present ' not in line:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        try:
            row = {key: int(fields[key]) for key in numeric}
            row.update({key: fields[key] for key in ('cmd', 'submit_queue', 'present_queue', 'coverage', 'reuse_safe')})
            rows.append(row)
        except (KeyError, ValueError):
            malformed += 1
    return {'coverage': 'partial' if rows else 'missing', 'samples': len(rows), 'malformed': malformed,
            'same_queue_samples': sum(r['submitted'] > 0 and r['same_queue'] == 1 for r in rows),
            'unobserved_submission_samples': sum(r['submitted'] == 0 for r in rows),
            'later_barrier_samples': sum(r['later_barriers'] > 0 for r in rows),
            'invalidated_samples': sum(r['invalidated'] != 0 for r in rows),
            'failed_samples': sum(r['submit_failed'] != 0 or r['result'] != 0 for r in rows),
            'samples_raw': rows, 'later_barriers': later_barriers, 'camera_policies': camera_policies,
            'sampled_input_observations': sampled_inputs, 'sampled_guide_observations': sampled_guides,
            'sampled_guide_coverage': 'descriptor_contract' if sampled_guides else 'missing',
            'resource_reuse_safe': 'unknown',
            'limits': 'CPU metadata only. Barriers do not cover render-pass writes or all secondary buffers; '
                      'same queue and no observed later barrier do not prove resource lifetime or FG compatibility.'}


def summarize_owned_vulkan(text):
    rows, gates = [], []
    evidence = {'fg_vulkan_depth_input': [], 'fg_vulkan_depth_barrier': [], 'fg_vulkan_input_gate': [], 'fg_vulkan_pacing': [], 'fg_vulkan_fault': [], 'fg_vulkan_reflex': []}
    required = {
        'fg_vulkan_reflex': ('enabled', 'result'),
        'fg_vulkan_fault': ('requested', 'actual', 'status', 'query', 'present', 'depth_snapshot'),
        'fg_vulkan_depth_input': ('frame', 'generation', 'recorded_layout', 'cached_layout', 'observed_layout', 'observed_known', 'descriptor_layout', 'aspect'),
        'fg_vulkan_depth_barrier': ('frame', 'same_cmd', 'old_layout', 'new_layout', 'aspect', 'source_family', 'destination_family', 'invalid', 'submissions'),
        'fg_vulkan_input_gate': ('token', 'tagged', 'depth', 'invalid', 'submissions', 'same_queue'),
        'fg_vulkan_pacing': ('frames', 'foreground_frames', 'window_ms', 'control_us', 'present_us', 'readback_us', 'max_present_us', 'enabled', 'active', 'ready', 'result')}
    evidence_malformed = 0
    malformed = 0
    for line in text.splitlines():
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        event = fields.get('event')
        if event in evidence:
            try:
                row = {key: int(fields[key]) for key in required[event]}
                if event == 'fg_vulkan_depth_input' and 'early_depth' in fields:
                    row['early_depth'] = int(fields['early_depth'])
                if event == 'fg_vulkan_input_gate':
                    for key in ('depth_submissions', 'depth_before_sr', 'depth_same_cmd', 'order_invalid'):
                        if key in fields:
                            row[key] = int(fields[key])
                evidence[event].append(row)
            except (KeyError, ValueError):
                evidence_malformed += 1
        if 'event=fg_vulkan_gate reason=' in line:
            gates.append(line.split('event=fg_vulkan_gate reason=', 1)[1])
        if 'event=fg_vulkan_frame ' not in line:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        try:
            row = {key: int(fields[key]) for key in ('requested', 'actual', 'status', 'query', 'present', 'depth_snapshot')}
            if 'colour_source' in fields:
                row['colour_source'] = fields['colour_source']
            for key in ('hudless', 'volatile_inputs'):
                if key in fields:
                    row[key] = int(fields[key])
            rows.append(row)
        except (KeyError, ValueError):
            malformed += 1
    usable = [row for row in rows if row['query'] == 0 and row['status'] == 0 and row['present'] in (0, 1000001003)]
    return {'coverage': 'sampled' if rows else 'missing', 'samples': len(rows), 'malformed': malformed,
            'generated_samples': sum(row['actual'] > 1 for row in usable),
            'gates': dict(Counter(gates)), 'samples_raw': rows,
            'input_evidence': evidence, 'input_evidence_malformed': evidence_malformed,
            'limits': 'Once-per-second runtime readback samples, not screen FPS or a complete multiplier distribution. '
                      'Camera may use configured approximations. Missing samples do not establish zero generation.'}


def summarize(text):
    rows = []
    for line in text.splitlines():
        if 'd18_fg_present api=' not in line:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line.split('d18_fg_present ', 1)[1]))
        try:
            row = {k: int(fields[k]) for k in ('query', 'status', 'presented', 'app_presents',
                   'warmup', 'enabled', 'active', 'paused', 'requested', 'present_hr')}
            row['api'] = fields['api']
            rows.append(row)
        except (KeyError, ValueError):
            continue
    usable = [r for r in rows if r['query'] == 0 and not r['warmup'] and r['present_hr'] == 0]
    groups = {}
    for r in usable:
        key = f"{r['api']}/enabled={r['enabled']}/requested={r['requested']}/active={r['active']}/paused={r['paused']}/status={r['status']}"
        g = groups.setdefault(key, {'windows': 0, 'presented_samples': [], 'app_presents': 0})
        g['windows'] += 1
        g['presented_samples'].append(r['presented'])
        g['app_presents'] += r['app_presents']
    for g in groups.values():
        g['presented_min'] = min(g['presented_samples'])
        g['presented_max'] = max(g['presented_samples'])
    return {'coverage': 'observed' if rows else 'missing', 'samples': len(rows),
            'query_results': dict(Counter(str(r['query']) for r in rows)),
            'runtime_statuses': dict(Counter(str(r['status']) for r in rows)),
            'groups': groups, 'samples_raw': rows, 'vulkan_input_contract': summarize_vulkan_contract(text),
            'owned_vulkan_fg': summarize_owned_vulkan(text),
            'limits': 'Samples are not screen FPS or a generation multiplier. Other consumers (including overlays) may query/reset the runtime counter between our reads; exclusive read ownership is unverified. Warmup and failed reads excluded; windows spanning setting changes may mix modes. Missing coverage is not zero generation.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(summarize(args.log.read_text(encoding='utf-8', errors='replace')), indent=2)
    if args.output:
        args.output.write_text(result, encoding='utf-8')
    else:
        print(result)
