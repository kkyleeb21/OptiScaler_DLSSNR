"""Build a host-only ABI regression from the real forwarder function.

Reconstructs the legacy ABI and compares corrected production; never loads NGX.
The original must fail compilation against a correctly typed callee; candidate
must compile and deliver the SDK version and null parameter pointer correctly.
"""
import difflib
import datetime
import hashlib
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'workspace/dlss5/worktrees/d18-012-re-integration/OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp'
OUT = ROOT / 'evidence/D24' / ('vk-init-abi-' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))


def main():
    OUT.mkdir(parents=True, exist_ok=False)
    production = SRC.read_text(encoding='utf-8')
    good_type = '                                       int, const void *);'
    good_call = 'g_vk.init(0x0, dataPath, instance, physicalDevice, device, sdkVersion, nullptr)'
    if production.count(good_type) != 1 or production.count(good_call) != 1:
        raise RuntimeError('production ABI drift')
    original = production.replace(good_type, '                                       const void *, int);').replace(
        good_call, 'g_vk.init(0x0, dataPath, instance, physicalDevice, device, nullptr, sdkVersion)')
    old_type = '                                       const void *, int);'
    old_call = 'g_vk.init(0x0, dataPath, instance, physicalDevice, device, nullptr, sdkVersion)'
    if original.count(old_type) != 1 or original.count(old_call) != 1:
        raise RuntimeError('source drift: review before generating candidate')
    candidate = original.replace(old_type, '                                       int, const void *);').replace(
        old_call, 'g_vk.init(0x0, dataPath, instance, physicalDevice, device, sdkVersion, nullptr)')
    diff = ''.join(difflib.unified_diff(original.splitlines(True), candidate.splitlines(True),
                                      fromfile='a/OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp',
                                      tofile='b/OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp'))
    (OUT / 'vk-init-order.patch').write_text(diff, encoding='utf-8')
    results = {}
    for name, source in [('original', original), ('candidate', candidate)]:
        typedef = re.search(r'using PFN_NrVkInitExt =.*?;', source, re.S).group()
        start = source.index('__declspec(dllexport) int dlssnr_vk_init(')
        brace = source.index('{', start)
        balance = 1
        end = brace + 1
        while balance:
            balance += (source[end] == '{') - (source[end] == '}')
            end += 1
        function = source[start:end]
        code = '#include <cstdio>\n' + typedef + r'''
static int calls = 0;
static int __cdecl expected(unsigned long long app, const wchar_t* path,
    void* instance, void* physical, void* device, int sdk, const void* parameters) {
    ++calls;
    return app == 0 && path && instance == reinterpret_cast<void*>(0x100) &&
        physical == reinterpret_cast<void*>(0x200) && device == reinterpret_cast<void*>(0x300) &&
        sdk == 0x15 && parameters == nullptr ? 1 : -99;
}
struct State { PFN_NrVkInitExt init = &expected; bool initialised = false; } g_vk;
int dlssnr_vk_last_init = 0;
bool loadVkSnippet(const wchar_t*) { return true; }
''' + function + r'''
int main() {
    int result = dlssnr_vk_init(L"not-loaded", L"not-written", reinterpret_cast<void*>(0x100),
        reinterpret_cast<void*>(0x200), reinterpret_cast<void*>(0x300), 0x15);
    if (result != 1 || dlssnr_vk_last_init != 1 || calls != 1) return 1;
    result = dlssnr_vk_init(L"not-loaded", L"not-written", nullptr, nullptr, nullptr, 0);
    if (result != 1 || calls != 1) return 2;
    puts("PASS: production function forwards SDKVersion=0x15, Parameters=null; initialization guard retained.");
}
'''
        cpp = OUT / (name + '.cpp')
        cpp.write_text(code, encoding='utf-8')
        command = f'call "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /Od "{cpp}" /Fe:"{OUT / (name + ".exe")}" /Fo:"{OUT / (name + ".obj")}"'
        batch = OUT / (name + '-build.cmd')
        batch.write_text('@echo off\n' + command + '\n', encoding='ascii')
        built = subprocess.run(['cmd.exe', '/d', '/c', str(batch)], cwd=OUT, capture_output=True, timeout=60)
        (OUT / (name + '-build.txt')).write_bytes(built.stdout + built.stderr)
        results[name] = dict(build_exit=built.returncode)
        if name == 'original':
            if built.returncode == 0 or b'C2440' not in built.stdout + built.stderr:
                raise RuntimeError('original did not fail for the expected callee type mismatch')
        else:
            if built.returncode != 0:
                raise RuntimeError('candidate compilation failed; inspect build log')
            run = subprocess.run([str(OUT / 'candidate.exe')], capture_output=True, timeout=10)
            (OUT / 'candidate-run.txt').write_bytes(run.stdout + run.stderr)
            results[name]['run_exit'] = run.returncode
            if run.returncode:
                raise RuntimeError('candidate ABI test failed')
    results['source_unchanged'] = SRC.read_text(encoding='utf-8') == production
    results['source_sha256'] = hashlib.sha256(SRC.read_bytes()).hexdigest()
    results['scope'] = 'host-only ABI regression; no GPU, no target DLL loading, candidate equals corrected production; no GPU'
    (OUT / 'result.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()

