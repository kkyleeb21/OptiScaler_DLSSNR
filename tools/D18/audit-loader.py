#!/usr/bin/env python3
"""File-only loader inventory using the shared D24 PE parser; never loads targets.

Records imports, delay imports, export coverage and bounded DLL-name observations.
Missing static observations are not proof that a DLL cannot be loaded dynamically.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'D24'))
from ngx_audit import PE

CANDIDATES = ('dxgi.dll', 'd3d12.dll', 'winmm.dll', 'version.dll', 'dbghelp.dll')
TOKENS = CANDIDATES + ('d3d11.dll', 'nvngx.dll', 'nvngx_dlss.dll', 'sl.interposer.dll',
                       'libxess.dll', 'ffx_fsr2_api_x64.dll', 'ffx_fsr3upscaler_x64.dll')


def inspect(path):
    data = path.read_bytes()
    row = {'path': str(path), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    try:
        pe = PE(data)
        row.update(imports=pe.imports() + pe.imports(delay=True), exports=pe.exports())
    except (ValueError, IndexError) as exc:
        row['parse_error'] = str(exc)
    lower = data.lower()
    row['dll_name_observations'] = {}
    for name in TOKENS:
        counts = {'ascii': lower.count(name.encode()), 'utf16le': lower.count(name.encode('utf-16le'))}
        if any(counts.values()):
            row['dll_name_observations'][name] = counts
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=pathlib.Path, required=True)
    parser.add_argument('--core', type=pathlib.Path, required=True)
    parser.add_argument('--system32', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error('use a new output file')
    paths = sorted(p for p in args.game.resolve().iterdir()
                   if p.is_file() and p.suffix.lower() in ('.dll', '.exe'))
    if len(paths) > 256:
        parser.error('root binary count exceeds bounded inventory limit of 256')
    core = inspect(args.core.resolve())
    if 'parse_error' in core:
        parser.error('core PE parse failed: ' + core['parse_error'])
    game = [inspect(p) for p in paths]
    system = [inspect(args.system32.resolve() / name) for name in
              (*CANDIDATES, 'd3d11.dll', 'd3d10.dll', 'd3d10_1.dll', 'd3d10core.dll')
              if (args.system32 / name).is_file()]
    names = {n for e in core['exports'] for n in e['names']}
    ordinals = {e['ordinal']: e for e in core['exports']}
    candidates = {}
    for name in CANDIDATES:
        official = next((r for r in system if pathlib.Path(r['path']).name.lower() == name), {})
        official_ordinals = {e['ordinal']: e for e in official.get('exports', [])}
        consumers = []
        for scope, modules in [('game_root', game), ('selected_system_modules', system)]:
            for module in modules:
                for entry in module.get('imports', []):
                    if entry['dll'].lower() != name:
                        continue
                    symbol = entry['symbol']
                    note = None
                    if symbol.startswith('ordinal:'):
                        ordinal = int(symbol.split(':')[1])
                        present = ordinal in ordinals
                        expected = official_ordinals.get(ordinal, {}).get('names', [])
                        actual = ordinals.get(ordinal, {}).get('names', [])
                        if expected != actual:
                            note = {'system_names': expected, 'core_names': actual}
                    elif symbol == 'UNRESOLVED_BOUND_IAT':
                        present = None
                    else:
                        present = symbol in names
                    consumers.append({'scope': scope, 'module': module['path'], **entry,
                                      'export_present': present, 'ordinal_name_difference': note})
        candidates[name] = {
            'existing_game_file': (args.game / name).is_file(),
            'import_observations': consumers,
            'missing_requested_exports': [c for c in consumers if c['export_present'] is False],
            'ordinal_identity_warnings': [c for c in consumers if c['ordinal_name_difference']],
            'system_named_exports_missing_from_core': sorted(
                {n for e in official.get('exports', []) for n in e['names']} - names),
            'runtime_load_status': 'not_observed',
        }
    report = {
        'schema': 'd18-loader-static-v1', 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'tool_sha256': hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
        'parser_sha256': hashlib.sha256((pathlib.Path(__file__).resolve().parents[1] / 'D24' / 'ngx_audit.py').read_bytes()).hexdigest(),
        'inspection': 'file-only; no game or inspected module executed',
        'scope': 'game-root EXE/DLL files and selected system DXGI/D3D/alternate-proxy modules; not a full dependency closure',
        'game': str(args.game.resolve()), 'core': core, 'game_modules': game, 'system_modules': system,
        'candidates': candidates, 'gameplay_verdict': 'not_tested',
        'limitations': ['Static imports do not establish runtime search paths, load order or hook execution.',
                        'No static name observation does not rule out constructed names or dynamic loading.',
                        'Export presence does not certify ABI forwarding or compatibility.',
                        'Unparsed binaries remain explicitly unknown.'],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2, ensure_ascii=False)
    print(json.dumps({'output': str(output), 'game_binaries': len(game),
                      'parse_errors': [r['path'] for r in game if 'parse_error' in r],
                      'core_sha256': core['sha256'],
                      'candidates': {n: {'game_importers': sorted({pathlib.Path(c['module']).name for c in v['import_observations'] if c['scope'] == 'game_root'}),
                                         'missing_exports': len(v['missing_requested_exports']),
                                         'ordinal_warnings': len(v['ordinal_identity_warnings'])} for n, v in candidates.items()}}, indent=2))


if __name__ == '__main__':
    main()
