"""Shared DX11 JSON, Vulkan text and D18 ring entry point; no gameplay verdict."""
import argparse
import collections
import json
import math
import struct
import importlib.util
from pathlib import Path


def summarize_dx11(path):
    events = collections.Counter()
    modes = collections.Counter()
    ranges = {}
    resources = collections.defaultdict(set)
    unavailable = collections.Counter()
    sampled = nonfinite = unparsed = 0
    crops = []
    allocation_failures = []
    parameter_roundtrips = []
    capture_progress = []
    capture_frames = []
    frame_contexts = []
    jitter_states = []
    previous = {}
    for line in path.read_text(encoding='utf-8-sig', errors='replace').splitlines():
        try:
            row = json.loads(line)
        except ValueError:
            unparsed += 1
            continue
        if not isinstance(row, dict):
            unparsed += 1
            continue
        events[row.get('event', 'legacy_fields')] += 1
        if row.get('event') == 'dx11_capture_progress':capture_progress.append(row)
        if row.get('event') == 'dx11_capture_frame_end':capture_frames.append(row)
        if row.get('event') == 'dx11_frame_context':frame_contexts.append(row)
        if row.get('event') == 'dx11_jitter_state':jitter_states.append(row)
        if row.get('event') == 'allocation_failed':
            allocation_failures.append(row)
        if row.get('event') == 'nr_parameter':
            parameter_roundtrips.append(row)
        if row.get('event') == 'dx11_crop':
            item = dict(row)
            # Only accept a sibling filename from the log.
            name = row['file']
            crop = path.parent / name
            if Path(name).name != name or not name.startswith('D24Capture_'):
                item['coverage'] = 'invalid_filename'
            elif not row['ok'] or not crop.is_file():
                item['coverage'] = 'missing_capture'
            else:
                try:
                    import numpy as np
                    decoder = load_adapter('capture_formats', Path(__file__).with_name('capture_formats.py'))
                    pixels = decoder.read_crop(path.parent, row)
                    rgb = pixels[..., :3].reshape(-1)
                    item.update(coverage='center_crop', **decoder.statistics(pixels[..., :3]))
                    old = previous.get(row['stage'])
                    rect = (row['x'], row['y'], row['width'], row['height'], row.get('format',10), row.get('run'))
                    if old and old[0]+1==row['frame'] and len(old[1])==len(rgb) and old[2]==rect:
                        valid=np.isfinite(rgb)&np.isfinite(old[1]);delta=np.abs(rgb[valid]-old[1][valid])
                        item['adjacent_mean_absolute_delta']=float(delta.mean()) if delta.size else None
                    previous[row['stage']] = (row['frame'], rgb, rect)
                except (OSError,ValueError) as exc:
                    item.update(coverage='unavailable', reason=str(exc))
            crops.append(item)
        if row.get('event') == 'dx11_inputs':
            sampled += 1
            modes[row['mode']] += 1
            nonfinite += not row['finite']
            for key in ('tick', 'call', 'reset', 'pre', 'mv_x', 'mv_y', 'jitter_x', 'jitter_y', 'render_width', 'render_height'):
                value = row[key]
                lo, hi = ranges.get(key, (value, value))
                ranges[key] = (min(lo, value), max(hi, value))
            for key in ('reset_result', 'pre_result'):
                if row[key] != 1:
                    unavailable[key] += 1
        elif row.get('event') == 'dx11_resource':
            resources[row['role']].add((row['resource'], row['width'], row['height'], row['format'], row['bind']))
            if row['result'] != 1:
                unavailable[row['role']] += 1
    return dict(source=str(path.resolve()), adapter='dx11-json',
                coverage='sampled_metadata' if sampled else 'dynamic_inputs_not_observed',
                sampled_inputs=sampled, sample_limit_reached=sampled >= 1800,
                modes=dict(modes), ranges=ranges, nonfinite_samples=nonfinite,
                unavailable_parameters=dict(unavailable),
                resource_variants={k: sorted(v) for k, v in resources.items()},
                events=dict(events), unparsed_lines=unparsed,
                capture_progress=capture_progress, capture_frame_results=capture_frames, frame_contexts=frame_contexts,
                jitter_states=jitter_states,
                crops=crops, allocation_failures=allocation_failures, parameter_roundtrips=parameter_roundtrips,
                gameplay_verdict='not_measured',
                limitations='Periodic metadata and optional region crops can miss transient defects. Input modes do not prove model execution. Crop readback affects timing. Deltas include scene motion and are not a flicker verdict. Resource rotation is not itself an error. Missing parameters may use defaults.')


def load_adapter(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def summarize(path):
    with path.open('rb') as stream:
        prefix = stream.read(4096)
    if prefix.startswith(b'D18DIAG'):
        adapter = load_adapter('d18_ring', Path(__file__).parent.parent / 'D18' / 'summarize-diagnostics.py')
        header, records = adapter.read_ring(path)
        result = adapter.summarize(header, records)
        result.update(source=str(path.resolve()), adapter='d18-ring', gameplay_verdict='not_measured')
    elif path.name.lower() == 'd24vulkandiagnostics.log' or any('event=' in line for line in prefix.decode('utf-8-sig', errors='replace').splitlines()
             if not line.lstrip().startswith('{')):
        result = load_adapter('d24_vulkan', Path(__file__).parent / 'vulkan' / 'summarize.py').summarize(path)
    else:
        result = summarize_dx11(path)
    result['summary_schema'] = 1
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(summarize(args.log), ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(result + '\n', encoding='utf-8')
    else:
        print(result)
