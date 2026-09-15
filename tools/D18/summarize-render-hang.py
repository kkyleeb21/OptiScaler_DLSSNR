"""Read-only correlation of existing render journals; never infers a deadlock root cause."""
import argparse
import hashlib
import json
from pathlib import Path


def summarize(root):
    files, rows = {}, {}
    for name in ('D18ExecutionTrace.jsonl', 'D24Native.log', 'D18WildlandsSR.jsonl', 'OptiScaler.log'):
        path = root / name
        if not path.is_file():
            files[name] = {'present': False}
            rows[name] = []
            continue
        raw = path.read_bytes()
        parsed, malformed = [], 0
        if name != 'OptiScaler.log':
            for number, line in enumerate(raw.decode('utf-8-sig', errors='replace').splitlines(), 1):
                if not line.strip():
                    continue
                try:
                    value = json.loads(line)
                    if not isinstance(value, dict):
                        raise ValueError('object expected')
                    parsed.append({'line': number, **value})
                except (ValueError, TypeError):
                    malformed += 1
        files[name] = {'present': True, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
                       'mtime_epoch': path.stat().st_mtime, 'malformed_lines': malformed}
        rows[name] = parsed
    nr = [r for r in rows['D18ExecutionTrace.jsonl'] if r.get('stage') == 'native_nr_return']
    errors = [r for r in rows['D24Native.log'] if r.get('event') == 'completion_failed']
    for error in errors:
        value = error.get('value')
        if isinstance(value, int):
            error['hresult_hex'] = f'0x{value & 0xffffffff:08X}'
            if value & 0xffffffff == 0x887A0027:
                error['hresult_name'] = 'DXGI_ERROR_WAIT_TIMEOUT'
    successes = [r for r in nr if r.get('b') == 1 and r.get('code') == 0]
    failures = [r for r in nr if r.get('code', 0) < 0]
    gaps = []
    for prev, cur in zip(nr, nr[1:]):
        if isinstance(prev.get('tick'), int) and isinstance(cur.get('tick'), int) and cur.get('code', 0) < 0:
            gaps.append({'previous_line': prev['line'], 'failure_line': cur['line'],
                         'delta_ms': cur['tick'] - prev['tick']})
    return {'schema': 'd18-render-hang-summary-v1', 'files': files,
            'nr_success_observations': len(successes), 'nr_failures': failures,
            'nr_last_observations': nr[-4:], 'completion_errors': errors, 'failure_gaps': gaps,
            'last_sr_record': rows['D18WildlandsSR.jsonl'][-1:] or None,
            'fg_text_unavailable': not files['OptiScaler.log'].get('bytes', 0),
            'boundary': 'Independent bounded journals, possibly stale or truncated. Verify build and process timestamps. '
                        'Completion timeout identifies an observed wait failure, not its originating stage or a proven GPU cycle. '
                        'No FG text does not mean no FG work. CPU dump stacks alone do not reconstruct GPU queue dependencies.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(summarize(args.bundle), indent=2), encoding='utf-8')
