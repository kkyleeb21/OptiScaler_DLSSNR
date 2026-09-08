#!/usr/bin/env python3
"""Validate a future native Feature-18 host's evidence without running GPU code.

Input is probe.json + frames.jsonl + packed/pitched readbacks under one directory.
No exports, pointers, log text or nonzero pixels alone constitute backend support.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import struct


def local_file(root, name):
    if not isinstance(name, str) or not name:
        raise ValueError('missing evidence path')
    p = (root / name).resolve()
    if not p.is_relative_to(root.resolve()):
        raise ValueError('evidence path escapes run directory')
    return p


def pixels(raw, width, height, fmt, pitch):
    if fmt not in ('RGBA8_UNORM', 'RGBA16_FLOAT'):
        raise ValueError('unsupported input format')
    stride = 4 if fmt == 'RGBA8_UNORM' else 8
    if not (isinstance(width, int) and isinstance(height, int) and 0 < width <= 16384 and 0 < height <= 16384):
        raise ValueError('invalid dimensions')
    if not isinstance(pitch, int) or pitch < width * stride or len(raw) != pitch * height:
        raise ValueError('invalid row pitch or readback size')
    packed = b''.join(raw[y * pitch:y * pitch + width * stride] for y in range(height))
    decoded = (tuple(v / 255 for v in p) for p in struct.iter_unpack('4B', packed)) if stride == 4 else struct.iter_unpack('<4e', packed)
    nonzero = finite = 0
    for pixel in decoded:
        rgb = pixel[:3]  # Opaque alpha must not make black output count as nonzero.
        finite += int(all(math.isfinite(v) for v in pixel))
        nonzero += int(all(math.isfinite(v) for v in rgb) and any(v != 0 for v in rgb))
    count = width * height
    return dict(sha256=hashlib.sha256(packed).hexdigest(), finite_fraction=finite / count,
                nonzero_rgb_fraction=nonzero / count, packed_bytes=len(packed)), packed


def validate(root):
    root = pathlib.Path(root).resolve()
    meta = json.loads((root / 'probe.json').read_text(encoding='utf-8-sig'))
    frames = [json.loads(line) for line in (root / 'frames.jsonl').read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    issues = []
    def need(condition, message):
        if not condition:
            issues.append(message)
    need(meta.get('schema') == 1, 'schema must be 1')
    need(meta.get('api') in ('D3D11', 'D3D12', 'VULKAN', 'CUDA'), 'unknown API')
    need(meta.get('feature_id') == 18, 'must request Feature 18, not DLSS SR')
    need(meta.get('route') == 'native', 'bridge success cannot certify a native backend')
    need(meta.get('runtime_modified') is False, 'runtime modification status missing or modified')
    need(meta.get('bypass_used') is False, 'bypass status missing or bypass used')
    need(meta.get('init_result') == 1 and meta.get('create_result') == 1, 'Init/Create did not both succeed')
    need(meta.get('device_removed') is False, 'device removal status unknown or device removed')
    need(meta.get('validation_errors') == 0, 'validation error count missing or nonzero')
    need(meta.get('colour_space') in ('linear', 'srgb', 'PQ'), 'colour space missing')
    for key in ('runtime_sha256', 'host_sha256'):
        value = meta.get(key)
        need(isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdefABCDEF' for c in value), f'{key} invalid')
    for key in ('driver', 'gpu', 'adapter_id', 'caller_module', 'parameter_provider', 'contract_file'):
        need(isinstance(meta.get(key), str) and bool(meta[key]), f'{key} missing')
    contract = json.loads(local_file(root, meta.get('contract_file')).read_text(encoding='utf-8-sig'))
    for key in ('render_size', 'output_size', 'depth_format', 'mv_format', 'mv_scale', 'jitter', 'reset', 'exposure', 'flags'):
        need(key in contract, f'contract.{key} missing')
    need(len(frames) >= 60, 'fewer than 60 evaluate frames')
    samples = []
    previous = None
    seen_fences = {}
    for frame in frames:
        n = frame.get('frame')
        need(type(n) is int and (previous is None or n == previous + 1), 'frame IDs must be consecutive integers')
        previous = n if type(n) is int else None
        need(frame.get('evaluate_result') == 1, f'frame {n}: Evaluate failed')
        target, complete = frame.get('fence_target'), frame.get('fence_completed')
        fence = frame.get('fence_id')
        fence_valid = (isinstance(fence, str) and bool(fence) and type(target) is int and type(complete) is int and
                       0 < target < (1 << 64) - 1 and target <= complete < (1 << 64) - 1)
        need(fence_valid, f'frame {n}: GPU completion unproven')
        if fence_valid:
            need(target > seen_fences.get(fence, 0), f'frame {n}: fence target reused or went backwards')
            seen_fences[fence] = target
        stats, output = pixels(local_file(root, frame.get('output_file')).read_bytes(), meta['width'], meta['height'], meta['format'], meta['row_pitch'])
        _, before = pixels(local_file(root, frame.get('before_file')).read_bytes(), meta['width'], meta['height'], meta['format'], meta['row_pitch'])
        stats['frame'] = n
        stats['changed_from_before'] = output != before
        need(stats['finite_fraction'] == 1, f'frame {n}: NaN/Inf output')
        need(stats['changed_from_before'], f'frame {n}: output unchanged from pre-evaluate sentinel')
        samples.append(stats)
    # Deliberately distinct input cases must produce distinct outputs; catches constant-fill false positives.
    cases = {}
    for frame, stats in zip(frames, samples):
        case = frame.get('input_case')
        need(isinstance(case, str) and bool(case), 'input_case missing')
        cases.setdefault(str(case), set()).add(stats['sha256'])
    need(len(cases) >= 2, 'need two controlled input cases')
    need(len({s['sha256'] for s in samples}) >= 2, 'all output hashes identical; input sensitivity unproven')
    status = 'EVIDENCE_COMPLETE_REQUIRES_REVIEW' if not issues else 'INCOMPLETE_OR_FAILED'
    return dict(schema=1, status=status, issues=issues, samples=samples,
                native_usable='NOT_AUTOMATICALLY_CERTIFIED', state_layout='UNVERIFIED',
                notes=['Raw output measured excluding row padding and excluding alpha for nonzero counts.',
                       'Metadata is a host assertion; review provenance/validation logs and output quality.',
                       'Do not dump an opaque NGX handle as a presumed state pointer.',
                       'State layout needs independently traced object identity, bounds and field dataflow.',
                       'Compare both formats and a matching native D3D12 control before route selection.'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('run', type=pathlib.Path)
    args = ap.parse_args()
    try:
        result = validate(args.run)
    except (OSError, ValueError, KeyError, TypeError, struct.error) as exc:
        result = dict(status='INVALID_EVIDENCE', issues=[str(exc)])
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['status'] == 'EVIDENCE_COMPLETE_REQUIRES_REVIEW' else 2)


if __name__ == '__main__':
    main()
