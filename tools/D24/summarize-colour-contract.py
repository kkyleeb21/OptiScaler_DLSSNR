"""Summarize bounded Vulkan colour metadata without claiming visual correctness."""
import argparse
import json
import re
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('log', type=Path)
a = p.parse_args()
snapshots, changes = [], []
current = None
for line in a.log.read_text(encoding='utf-8', errors='replace').splitlines():
    fields = dict(re.findall(r'([\w.]+)=([^\s]+)', line))
    event = fields.get('event')
    if event == 'sr_snapshot':
        current = {'tick': int(fields['tick']), 'frame': int(fields['frame'])}
        snapshots.append(current)
    elif event == 'highlight_encoding':
        changes.append(fields)
    elif current is not None:
        if event == 'feature_flags':
            flags = int(fields['value']) if fields.get('known') == '1' else None
            current['flags'] = flags
            current['linear_hdr_flag'] = bool(flags & 1) if flags is not None else None
            current['sr_auto_exposure'] = bool(flags & 64) if flags is not None else None
        elif event == 'float' and fields.get('key') in ('DLSS.Pre.Exposure', 'DLSS.Exposure.Scale'):
            current[fields['key']] = float(fields['value']) if fields.get('result') == '1' else None
        elif event == 'resource' and fields.get('role') == 'exposure':
            current['exposure_texture_present'] = fields.get('present') == '1'
            current['exposure_texture_format'] = fields.get('format')
        elif event == 'uint' and fields.get('key') == 'Reset':
            current['reset'] = fields.get('value')
print(json.dumps({'snapshots': snapshots, 'encoding_changes': changes,
    'limitations': ['Snapshot-only sampling, not continuous exposure history.',
                   'Exposure texture presence does not reveal its value or prove use by SR.',
                   'DLSS HDR input flag does not identify display HDR state.',
                   'No pixel statistics or visual correctness conclusion.']}, indent=2))
