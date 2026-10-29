"""Offline installer regressions. Runtime inputs are read as data, never loaded.

Run with --scratch under the task build directory. Only local caller-supplied inputs
are used; all mutations and fake installations stay inside that directory.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest().upper()


def write_text(path, text, bom=False):
    path.write_text(text, encoding='utf-8-sig' if bom else 'utf-8')
    assert path.read_text(encoding='utf-8-sig') == text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scratch', required=True, type=Path)
    parser.add_argument('--nr3108', required=True, type=Path)
    parser.add_argument('--official', required=True, type=Path)
    parser.add_argument('--compat', required=True, type=Path)
    parser.add_argument('--setup', type=Path, help='Optional compiled WPF assembly for message checks')
    args = parser.parse_args()
    installer = Path(__file__).resolve().parents[1]
    scratch = args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, TEMP=str(scratch), TMP=str(scratch))
    shell = str(Path(os.environ['SYSTEMROOT']) / 'System32/WindowsPowerShell/v1.0/powershell.exe')
    commands = []

    def run(name, script, *arguments, expected=0):
        command = [shell, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(script), *map(str, arguments)]
        result = subprocess.run(command, capture_output=True, env=env)
        # Windows PowerShell console output uses the host code page. JSON files use UTF-8.
        (scratch / (name + '.stdout.bin')).write_bytes(result.stdout)
        (scratch / (name + '.stderr.bin')).write_bytes(result.stderr)
        commands.append(dict(name=name, argv=command, exit_code=result.returncode))
        write_text(scratch / 'commands.json', json.dumps(commands, indent=2))
        assert result.returncode == expected, (name, result.returncode, result.stderr.decode('utf-8', errors='replace'))
        return result

    runner = scratch / 'classify.ps1'
    write_text(runner, r'''param($Common,$InputFile,$OutputFile,$Manifest,$Result)
$ErrorActionPreference='Stop'
. $Common
try {
    $value=New-D18PatchedRuntime $InputFile $OutputFile $Manifest
} catch {
    if(-not $_.Exception.Data.Contains('D18RuntimeConflict')){throw}
    $value=$_.Exception.Data['D18RuntimeConflict']
}
$value|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $Result -Encoding UTF8
''', bom=True)
    manifest = json.loads((installer / 'runtime_patch.json').read_text(encoding='utf-8-sig'))
    profiles = {p['id']: p for p in manifest['profiles']}
    inputs = [args.nr3108.resolve(), args.official.resolve(), args.compat.resolve()]
    input_hashes = {str(p): sha(p) for p in inputs}
    rows = []

    def classify(name, source, manifest_path=None, expected='VERIFIED', reason='', detected=None):
        output = scratch / (name + '-output.dll')
        output.write_bytes(b'unchanged output sentinel')
        before = sha(source)
        result_path = scratch / (name + '.json')
        run(name, runner, installer / 'D18-Common.ps1', source, output,
            manifest_path or installer / 'runtime_patch.json', result_path)
        data = json.loads(result_path.read_text(encoding='utf-8-sig'))
        assert data['Classification'] == expected, (name, data)
        assert data['ConflictReason'] == reason, (name, data)
        if detected is not None:
            assert data['DetectedVersion'] == detected, (name, data)
        assert before == sha(source), name + ' changed source'
        if reason:
            assert output.read_bytes() == b'unchanged output sentinel', name + ' changed output'
            assert data['SupportedVersions'] == ['310.8', '310.9.16'], data
            for key in ('MessageEn', 'MessageZh'):
                assert all(v in data[key] for v in data['SupportedVersions']), data
                assert (detected or ('unknown' if key == 'MessageEn' else '\u672a\u77e5')) in data[key], data
                assert '???' not in data[key] and '\ufffd' not in data[key], data
            for key, suffix in (
                ('MessageEn', 'Select an nvngx_dlssnr.dll with one of these versions.'),
                ('MessageZh', '\u8bf7\u6362\u6210\u8fd9\u4e9b\u7248\u672c\u7684 nvngx_dlssnr.dll\u3002'),
            ):
                assert (suffix in data[key]) == (reason not in (
                    'GUARD_BYTES_MISMATCH', 'PATCH_STATE_INCOMPLETE')), data
        rows.append(dict(name=name, result=data))
        return output, data

    for name, source, profile, status, version in [
        ('nr3108', inputs[0], 'nr3108', 'VERIFIED', '310.8.0.0'),
        ('official', inputs[1], 'nr310916-official', 'VERIFIED', '310.9.16.0'),
        ('compat', inputs[2], 'nr310916-compat', 'EXPERIMENTAL_UNSIGNED', '310.9.16.0'),
    ]:
        output, data = classify(name, source, expected=status, detected=version)
        assert data['OutputSha256'] == profiles[profile]['reference_output_sha256'], data
        _, repeated = classify(name + '-repeated', output, expected=status, detected=version)
        assert repeated['AppliedHunks'] == 0, repeated

    original = inputs[1].read_bytes()

    def mutate(name, change, reason, detected='310.9.16.0'):
        data = bytearray(original)
        change(data)
        path = scratch / (name + '.dll')
        path.write_bytes(data)
        _, result = classify(name, path, expected='CONFLICT', reason=reason, detected=detected)
        return path, result

    def version_change(build):
        def change(data):
            # VS_FIXEDFILEINFO in the existing version resource, no resource loading.
            key = data.index('VS_VERSION_INFO\0'.encode('utf-16-le'))
            fixed = (key + len('VS_VERSION_INFO\0'.encode('utf-16-le')) + 3) & ~3
            assert struct.unpack_from('<I', data, fixed)[0] == 0xFEEF04BD
            struct.pack_into('<II', data, fixed + 8, (310 << 16) | 9, build << 16)
        return change

    negative = []
    negative.append(mutate('unsupported-310917', version_change(17), 'UNSUPPORTED_VERSION', '310.9.17.0'))
    negative.append(mutate('unsupported-31091', version_change(1), 'UNSUPPORTED_VERSION', '310.9.1.0'))

    def remove_resource(data):
        pe = struct.unpack_from('<I', data, 60)[0]
        assert struct.unpack_from('<H', data, pe + 24)[0] == 0x20B
        struct.pack_into('<II', data, pe + 24 + 112 + 2 * 8, 0, 0)

    negative.append(mutate('no-version-resource', remove_resource, 'NOT_NR_RUNTIME', ''))
    for name, replacement, text in [
        ('sr-resource', 'DLSSSR', 'super resolution'),
        ('rr-resource', 'DLSSD ', 'ray reconstruction'),
        ('fg-resource', 'DLSSG ', 'frame generation'),
    ]:
        def replace_identity(data, replacement=replacement):
            old = 'DLSSNR'.encode('utf-16-le')
            new = replacement.encode('utf-16-le')
            assert old in data and len(old) == len(new)
            data[:] = data.replace(old, new)
            # A non-NR layout must already fail admission; do not add a new metadata gate.
            data[profiles['nr310916-official']['guards'][0]['offset']] ^= 1
        path, result = mutate(name, replace_identity, 'NOT_NR_RUNTIME')
        assert text in result['MessageEn'], result
        negative.append((path, result))

    def bad_guard(data):
        data[profiles['nr310916-official']['guards'][0]['offset']] ^= 1
    negative.append(mutate('bad-guard', bad_guard, 'GUARD_BYTES_MISMATCH'))

    def partial_patch(data):
        for hunk in profiles['nr310916-official']['hunks']:
            replacement = base64.b64decode(''.join(hunk.get('replacement_base64_parts', [])) or hunk.get('replacement_base64', ''))
            expected = base64.b64decode(hunk['expected_base64'])
            if hunk['offset'] > 4096 and expected and expected != replacement and len(expected) == len(replacement):
                data[hunk['offset']:hunk['offset'] + len(replacement)] = replacement
                return
        raise AssertionError('no non-neutral hunk')
    negative.append(mutate('partial-patch', partial_patch, 'PATCH_STATE_INCOMPLETE'))

    # Missing optional metadata: exercise actual v3 layout under StrictMode, and tiny v2.
    optional = json.loads(json.dumps(manifest))
    for profile in optional['profiles']:
        for field in ('notice', 'reference_source_sha256', 'reference_output_sha256'):
            profile.pop(field, None)
    optional_path = scratch / 'optional-fields.json'
    write_text(optional_path, json.dumps(optional))
    _, optional_result = classify('missing-v3-optionals', inputs[1], optional_path, expected='UNVERIFIED_COMPATIBLE')
    assert optional_result['Notice'] == '' and not optional_result['Experimental'], optional_result
    v2 = scratch / 'legacy-v2.json'
    write_text(v2, json.dumps(dict(format='dlssnr-d18-guarded-layout-patch-v2', hunks=[dict(
        offset=1, expected_base64='AgM=', replacement_base64='FB4=')])) )
    tiny = scratch / 'legacy-input.dll'
    tiny.write_bytes(bytes([1, 2, 3, 4]))
    _, legacy = classify('legacy-no-notice', tiny, v2, expected='UNVERIFIED_COMPATIBLE')
    assert legacy['Notice'] == '', legacy

    # Fake CLI package. No core or Runtime DLL is loaded or executed.
    package = scratch / 'package'
    payload = package / 'payload'
    payload.mkdir(parents=True)
    for name in ('Install-D18.ps1', 'D18-Common.ps1', 'D18-REFramework.ps1', 'D18-Dependencies.ps1', 'reframework-versions.json', 'runtime_patch.json'):
        shutil.copyfile(installer / name, package / name)
    (payload / 'OptiScaler.dll').write_bytes(bytes([1, 2, 3]))
    write_text(payload / 'OptiScaler.ini.d18', '[Menu]\nShortcutKey=auto\n')
    write_text(package / 'payload_manifest.json', json.dumps(dict(release_name='Offline test', release_version='0.5.0', files=[
        dict(path=p.name, sha256=sha(p)) for p in payload.iterdir()])))
    game = scratch / 'fake-game'
    game.mkdir()
    write_text(game / 'Fixture.exe', 'inert EXE sentinel, never executed')
    for path, expected in negative:
        name = path.stem
        request = scratch / (name + '-request.json')
        result = scratch / (name + '-bridge.json')
        write_text(request, json.dumps(dict(action='ValidateNr', runtime=str(path))))
        run(name + '-bridge', installer / 'D18-InstallerBridge.ps1', '-Offline', '-RequestPath', request, '-ResultPath', result, expected=1)
        bridge = json.loads(result.read_text(encoding='utf-8-sig'))
        assert not bridge['success'] and bridge['code'] == 'runtime_conflict', bridge
        assert bridge['data'] == expected, bridge
        result = scratch / (name + '-cli.json')
        run(name + '-cli', package / 'Install-D18.ps1', '-GameDir', game, '-RuntimePath', path,
            '-NativeApi', 'None', '-ProxyName', 'dxgi.dll', '-Yes', '-CheckOnly', '-ResultPath', result, expected=1)
        cli = json.loads(result.read_text(encoding='utf-8-sig'))
        assert not cli['success'] and cli['code'] == 'runtime_conflict', cli
        assert cli['data'] == expected, cli
        assert set(p.name for p in game.iterdir()) == {'Fixture.exe'}, 'fake game mutated'
        result = scratch / (name + '-worker.json')
        write_text(request, json.dumps(dict(action='Check', runtime=str(path), exe=str(game / 'Fixture.exe'),
            game=str(game), api='None', proxy='dxgi.dll', ref='Manual', srMode='Keep', fgMode='Keep',
            backendRoot=str(package), cache=str(scratch / 'cache'))))
        run(name + '-worker', installer / 'D18-GuiWorker.ps1', '-RequestPath', request, '-ResultPath', result, expected=1)
        worker = json.loads(result.read_text(encoding='utf-8-sig'))
        assert not worker['success'] and worker['code'] == 'runtime_conflict', worker
        assert all(worker['data'][key] == value for key, value in expected.items()), worker
        assert set(p.name for p in game.iterdir()) == {'Fixture.exe'}, 'worker mutated fake game'

    if args.setup:
        run('wpf-messages', installer / 'tests/Test-RuntimeConflictMessages.ps1',
            '-SetupPath', args.setup.resolve(), '-ResultsRoot', scratch)

    assert all(sha(Path(p)) == digest for p, digest in input_hashes.items()), 'original input changed'
    write_text(scratch / 'results.json', json.dumps(dict(passed=True, cases=rows, input_hashes=input_hashes,
        bridge_cases=len(negative), cli_cases=len(negative), worker_cases=len(negative),
        wpf_messages=bool(args.setup)), ensure_ascii=False, indent=2))
    print(f'PASS {len(rows)} classification/patch cases, {len(negative)} bridge, CLI and worker conflicts each; originals unchanged')


if __name__ == '__main__':
    main()
