"""Local build evidence. Collection failures are explicit unknowns, never guessed versions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from datetime import datetime, timezone

SCHEMA = 'd18-build-provenance-v1'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest().upper()


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def query(command, cwd=None, env=None):
    try:
        result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, timeout=60)
        output = (result.stdout + result.stderr).decode('utf-8', errors='replace').strip()
        return {'command': list(map(str, command)), 'exit_code': result.returncode,
                'output': output, 'status': 'known' if result.returncode == 0 else 'unknown'}
    except (OSError, subprocess.SubprocessError) as error:
        return {'command': list(map(str, command)), 'status': 'unknown', 'reason': str(error)}


def source_state(root, patch_path=None):
    root = Path(root).resolve()
    head = query(['git', '-C', str(root), 'rev-parse', 'HEAD'])
    if head['status'] != 'known':
        return {'root': str(root), 'commit': 'unknown', 'branch': 'unknown', 'dirty': 'unknown',
                'changed_files': [], 'patch_sha256': 'unknown', 'patch_path': 'unknown',
                'submodules': {'status': 'unknown', 'reason': 'Git information unavailable'}, 'reason': head}
    try:
        def git(*args):
            return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE)
        tracked = git('diff', '--name-only', '-z', 'HEAD').split(b'\0')
        untracked = git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0')
        names = sorted({n.decode('utf-8') for n in tracked + untracked if n})
        patch = git('diff', '--binary', 'HEAD')
        for name in sorted(n.decode('utf-8') for n in untracked if n):
            result = subprocess.run(['git', 'diff', '--no-index', '--binary', '--', '/dev/null', name],
                                    cwd=root, capture_output=True)
            if result.returncode not in (0, 1):
                raise RuntimeError(result.stderr.decode(errors='replace'))
            patch += result.stdout
        branch = git('rev-parse', '--abbrev-ref', 'HEAD').decode().strip()
        if patch_path is not None:
            Path(patch_path).write_bytes(patch)
        return {'root': str(root), 'commit': head['output'], 'branch': branch, 'dirty': bool(names),
                'changed_files': names, 'patch_sha256': hashlib.sha256(patch).hexdigest().upper(),
                'patch_path': str(Path(patch_path).resolve()) if patch_path is not None else 'not archived by this metadata-only query',
                'patch_format': 'git diff --binary HEAD followed by sorted untracked git diff --no-index /dev/null',
                'submodules': query(['git', '-C', str(root), 'submodule', 'status', '--recursive'])}
    except (OSError, subprocess.SubprocessError, RuntimeError, UnicodeError) as error:
        return {'root': str(root), 'commit': head['output'], 'branch': 'unknown', 'dirty': 'unknown',
                'changed_files': [], 'patch_sha256': 'unknown', 'patch_path': 'unknown',
                'submodules': {'status': 'unknown', 'reason': 'Git snapshot failed'}, 'reason': str(error)}


def file_record(path):
    p = Path(path).resolve()
    return {'path': str(p), 'name': p.name, 'size': p.stat().st_size, 'sha256': sha(p)}


def tree_record(path):
    p = Path(path).resolve()
    if not p.is_dir():
        return {'path': str(p), 'status': 'unknown', 'reason': 'Directory unavailable'}
    try:
        rows = [{'path': f.relative_to(p).as_posix(), 'size': f.stat().st_size, 'sha256': sha(f)}
                for f in sorted(p.rglob('*')) if f.is_file() and '.git' not in f.relative_to(p).parts]
        digest = hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(',', ':')).encode()).hexdigest().upper()
        return {'path': str(p), 'status': 'known', 'files': len(rows), 'tree_sha256': digest,
                'hash_format': 'SHA256 of UTF-8 canonical JSON rows (path,size,sha256), sorted by path; .git excluded'}
    except OSError as error:
        return {'path': str(p), 'status': 'unknown', 'reason': str(error)}


def tool_record(name, args, env):
    name = str(name) if name is not None else None
    p = shutil.which(name, path=env.get('PATH')) if name else None
    if not p:
        return {'status': 'unknown', 'reason': 'Tool not found in build environment', 'requested': name}
    record = file_record(p)
    record['version'] = query([p, *args], env=env)
    # cl without an input exits nonzero but prints the actual compiler version banner.
    if Path(p).name.lower() == 'cl.exe' and 'Version ' in record['version'].get('output', ''):
        record['version']['status'] = 'known'
    return record


def begin(root, dependency_root=None, env=None, msbuild=None, dxc=None, patch_path=None):
    env = os.environ if env is None else env
    root = Path(root).resolve()
    dep = Path(dependency_root or root)
    bundled_dxc=dep/'OptiScaler/shaders/shader_tools/dxc.exe'
    if not dxc and bundled_dxc.is_file(): dxc=str(bundled_dxc)
    dependencies = []
    for parent in [dep / 'external', dep / 'OptiScaler/library']:
        if parent.is_dir():
            dependencies.extend(tree_record(p) for p in sorted(parent.iterdir()) if p.is_dir())
        else:
            dependencies.append({'path': str(parent), 'status': 'unknown', 'reason': 'Directory unavailable'})
    # These headers are build inputs. They are not regenerated by the binary build.
    headers = sorted(set((root / 'OptiScaler/shaders').glob('**/precompil*/*.h')) |
                     set((root / 'tools/D24/bg3-native').glob('*shader.h')))
    sdk = env.get('WindowsSDKVersion', env.get('WINDOWSSDKVERSION', 'unknown')).rstrip('\\')
    return {'schema': SCHEMA, 'started_at_utc': datetime.now(timezone.utc).isoformat(),
            'source': source_state(root, patch_path), 'dependencies': dependencies,
            'tools': {'msvc': tool_record('cl.exe', [], env),
                      'msbuild': tool_record(msbuild or 'MSBuild.exe', ['-version', '-nologo'], env),
                      'windows_sdk': {'version': sdk, 'version_source': 'WindowsSDKVersion in the actual build environment (unknown if absent)',
                                      'root': env.get('WindowsSdkDir', env.get('WINDOWSSDKDIR', 'unknown')),
                                      'rc': tool_record('rc.exe', ['/?'], env)},
                      'dxc': tool_record(dxc or 'dxc.exe', ['--version'], env),
                      'csc': tool_record(str(Path(env.get('SYSTEMROOT', 'C:/Windows')) / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'), ['/help'], env)},
            'environment': {k: env[k] for k in ['CL', '_CL_', 'LINK'] if k in env},
            'shaders': {'commands': [], 'status': 'retained_precompiled_inputs',
                        'reason': 'No shader compilation executed by this binary build. Original header generation commands are unknown.',
                        'headers': [file_record(p) for p in headers]},
            'commands': []}


def record_command(record, argv, cwd=None, properties=None):
    entry = {'argv': list(map(str, argv)), 'cwd': str(Path(cwd or os.getcwd()).resolve()),
             'properties': properties or {}}
    record['commands'].append(entry)
    return entry


def finish(record, output, binaries):
    record['completed_at_utc'] = datetime.now(timezone.utc).isoformat()
    record['outputs'] = [file_record(p) for p in binaries]
    save(output, record)
    return record


def verify(provenance, binaries):
    record = json.loads(Path(provenance).read_text(encoding='utf-8-sig'))
    if record.get('schema') != SCHEMA:
        raise RuntimeError('Unsupported build provenance: ' + str(provenance))
    for binary in binaries:
        actual = file_record(binary)
        matches = [row for row in record['outputs'] if row['name'] == actual['name']]
        if len(matches) != 1 or any(matches[0].get(k) != actual[k] for k in ('size', 'sha256')):
            raise RuntimeError('Build provenance binary mismatch: ' + str(binary))
    return record


def combine(paths, target):
    records = [json.loads(Path(p).read_text(encoding='utf-8-sig')) for p in paths]
    save(target, {'schema': SCHEMA, 'components': records,
                  'outputs': [row for record in records for row in record['outputs']]})


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--source', required=True, type=Path)
    p.add_argument('--dependency-root', type=Path)
    p.add_argument('--phase', choices=['begin','finish'], default='finish')
    p.add_argument('--commands', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--binary', action='append', type=Path)
    a = p.parse_args()
    # Native PowerShell wrapper captures the snapshot before invoking its first compiler.
    if a.phase == 'begin':
        save(a.output, begin(a.source, a.dependency_root, patch_path=a.output.with_name('build-source.patch')))
    else:
        record = json.loads(a.commands.read_text(encoding='utf-8-sig'))
        finish(record, a.output, a.binary)


if __name__ == '__main__':
    main()
