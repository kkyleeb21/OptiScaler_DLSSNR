"""Compile actual bridge methods against a delayed/failing GPU-fence model.

Checks the observable write ordering, including device removal while waiting.
This does not replace a real DX11/DX12 GPU or overlay integration test.
"""
import argparse
import os
from pathlib import Path
import subprocess
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
text = args.source.read_text(encoding='utf-8')

def method(name):
    start = text.index('bool Dx11wDx12SC::' + name + '(')
    end = text.index('\nbool Dx11wDx12SC::', start + 1)
    return text[start:end]

prefix = r'''
#include <windows.h>
#include <cstdint>
#include <vector>
#include <iostream>
#define LOG_ERROR(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define IID_PPV_ARGS(p) p
struct ID3D11Texture2D { void Release() {} };
struct Fence {
    UINT64 completed = 0;
    HRESULT eventResult = S_OK;
    bool removedDuringWait = false;
    UINT64 GetCompletedValue() { return completed; }
    HRESULT SetEventOnCompletion(UINT64 value, HANDLE event) {
        if (FAILED(eventResult)) return eventResult;
        completed = removedDuringWait ? UINT64_MAX : value;
        SetEvent(event);
        return S_OK;
    }
};
struct Context {
    Fence* fence;
    int copies = 0, unsafeWrites = 0;
    void CopyResource(ID3D11Texture2D*, ID3D11Texture2D*) {
        ++copies;
        if (fence->completed < 7 || fence->completed == UINT64_MAX) ++unsafeWrites;
    }
};
struct Swapchain {
    ID3D11Texture2D texture;
    HRESULT GetBuffer(UINT, ID3D11Texture2D** out) { *out = &texture; return S_OK; }
};
struct Dx11wDx12SC {
    UINT _currentFakeIndex = 0;
    std::vector<ID3D11Texture2D*> _sharedDx11BackBufferCopies;
    Context* _dx11Context;
    Swapchain* _real;
    Fence* _copyFence;
    HANDLE _copyFenceEvent;
    std::vector<UINT64> _copyAllocatorFenceValues {7};
    bool _CopyDx11BackBufferToShared(UINT);
    bool _WaitForCopyAllocator(UINT);
};
'''
suffix = r'''
int main() {
    int failures = 0;
    for (int scenario = 0; scenario < 5; ++scenario) {
        Fence fence;
        if (scenario == 0) fence.completed = 7;
        if (scenario == 2) fence.eventResult = E_FAIL;
        if (scenario == 3) fence.completed = UINT64_MAX;
        if (scenario == 4) fence.removedDuringWait = true;
        Context context{&fence};
        Swapchain swapchain;
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        Dx11wDx12SC bridge;
        bridge._sharedDx11BackBufferCopies.push_back(&swapchain.texture);
        bridge._dx11Context = &context;
        bridge._real = &swapchain;
        bridge._copyFence = &fence;
        bridge._copyFenceEvent = event;
        bool ok = bridge._CopyDx11BackBufferToShared(0);
        bool expected = scenario < 2;
        if (ok != expected || context.copies != (expected ? 1 : 0) || context.unsafeWrites) {
            std::cerr << "FAIL scenario=" << scenario << " result=" << ok
                      << " copies=" << context.copies << " unsafe=" << context.unsafeWrites << '\n';
            ++failures;
        }
        CloseHandle(event);
    }
    std::cout << "5 scenarios, failures=" << failures << '\n';
    return failures ? 1 : 0;
}
'''
cpp = args.output / 'shared-reuse-test.cpp'
cpp.write_text(prefix + method('_CopyDx11BackBufferToShared') + '\n' + method('_WaitForCopyAllocator') + suffix)
envtext = subprocess.check_output(['cmd', '/c', r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'], text=True)
env = dict(line.split('=', 1) for line in envtext.splitlines() if '=' in line and not line.startswith('='))
env = {k.upper(): v for k, v in env.items()}
exe = args.output / 'shared-reuse-test.exe'
compiler = shutil.which('cl.exe', path=env['PATH']) or r'C:\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe'
subprocess.run([compiler, '/nologo', '/EHsc', '/std:c++20', str(cpp), '/Fe:' + str(exe), '/Fo:' + str(args.output / 'test.obj')], env=env, check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
