"""Compile the production wheel observer in an isolated Win32 fixture.

Posts messages only to its own hidden windows; does not inject hardware input,
touch games, change another process's raw registration, or emulate gameplay.
"""
import argparse
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--vcvars", default=r"C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "OptiScaler/menu/input/input_system_detours.cpp").read_text(encoding="utf-8")
    observer = source[source.index("static LRESULT CALLBACK PollingWheelProc"):source.index("// Onimusha's polling UI:")]
    raw = (ROOT / "OptiScaler/menu/input/input_system_raw.cpp").read_text(encoding="utf-8")
    raw = raw[raw.index("RawInputSanitizeDecision GetRawInputSanitizeDecisionLocked"):raw.index("void RecordRawInputSanitizeCounterLocked")]
    preamble = r'''
#include <windows.h>
#include <array>
#include <mutex>
#include <vector>
#include <cstdio>
#include <cstdlib>
#define LOG_WARN(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define CHECK(x) do {if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
struct RawInputSanitizeDecision {HRAWINPUT Handle=nullptr;int Action=0;USHORT AllowedMouseButtonUpFlags=0;};
struct Fixture {
 std::recursive_mutex Mutex;
 bool PollingOnly=true,MenuVisible=true,Focused=true,PollingWheelUsesRaw=false;
 bool ReceivedQueueMessageThisFrame=false,ReceivedRawInputThisFrame=false,ReceivedAnyInputThisFrame=false;
 HWND InputHwnd=nullptr,TargetHwnd=nullptr,TargetRootHwnd=nullptr;
 HHOOK PollingWheelHook=nullptr;DWORD PollingWheelThread=0;
 float MouseWheel=0,MouseWheelH=0;
 std::array<RawInputSanitizeDecision,128> RawInputSanitizeCache{};
 size_t RawInputSanitizeCacheWriteIndex=0;
} _state;
bool ShouldBlockMouseInputLocked(){return _state.MenuVisible&&_state.Focused;}
int GetRawInputSanitizeActionLocked(const RAWINPUT&,USHORT*){return 0;}
'''
    tests = r'''
static void queued(HWND hwnd,UINT type,int delta,bool peek=false) {
 CHECK(PostMessageW(hwnd,type,MAKEWPARAM(0,SHORT(delta)),0));
 MSG msg{};
 CHECK(PeekMessageW(&msg,hwnd,type,type,peek?PM_NOREMOVE:PM_REMOVE));
 if(peek) CHECK(PeekMessageW(&msg,hwnd,type,type,PM_REMOVE));
}
int main() {
 _state.InputHwnd=CreateWindowExW(0,L"STATIC",L"D18 isolated wheel fixture",WS_POPUP,0,0,600,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 CHECK(_state.InputHwnd);_state.TargetHwnd=_state.TargetRootHwnd=_state.InputHwnd;
 UpdatePollingWheelHookLocked();CHECK(_state.PollingWheelHook);CHECK(!_state.PollingWheelUsesRaw);
 queued(_state.InputHwnd,WM_MOUSEWHEEL,-120,true);CHECK(_state.MouseWheel==-1);
 queued(_state.InputHwnd,WM_MOUSEWHEEL,-30);CHECK(_state.MouseWheel==-1.25f);
 queued(_state.InputHwnd,WM_MOUSEHWHEEL,120);CHECK(_state.MouseWheelH==-1);
 HWND other=CreateWindowExW(0,L"STATIC",L"Unrelated fixture",WS_POPUP,0,0,50,50,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 CHECK(other);queued(other,WM_MOUSEWHEEL,120);CHECK(_state.MouseWheel==-1.25f);
 _state.Focused=false;queued(_state.InputHwnd,WM_MOUSEWHEEL,120);CHECK(_state.MouseWheel==-1.25f);
 _state.Focused=true;_state.MenuVisible=false;queued(_state.InputHwnd,WM_MOUSEWHEEL,120);CHECK(_state.MouseWheel==-1.25f);
 UpdatePollingWheelHookLocked();CHECK(!_state.PollingWheelHook);
 _state.MenuVisible=true;
 // The isolated fixture owns this registration, and removes it before exiting.
 RAWINPUTDEVICE device{1,2,RIDEV_NOLEGACY,_state.InputHwnd};
 CHECK(RegisterRawInputDevices(&device,1,sizeof(device)));
 Sleep(1050);UpdatePollingWheelHookLocked();CHECK(_state.PollingWheelHook);CHECK(_state.PollingWheelUsesRaw);
 queued(_state.InputHwnd,WM_MOUSEWHEEL,-120);CHECK(_state.MouseWheel==-1.25f);
 RAWINPUT packet{};packet.header.dwType=RIM_TYPEMOUSE;packet.header.dwSize=sizeof(packet);
 packet.data.mouse.usButtonFlags=RI_MOUSE_WHEEL;packet.data.mouse.usButtonData=USHORT(SHORT(-120));
 const auto handle=reinterpret_cast<HRAWINPUT>(uintptr_t(123));
 GetRawInputSanitizeDecisionLocked(handle,packet);CHECK(_state.MouseWheel==-2.25f);
 GetRawInputSanitizeDecisionLocked(handle,packet);CHECK(_state.MouseWheel==-2.25f);
 _state.Focused=false;GetRawInputSanitizeDecisionLocked(reinterpret_cast<HRAWINPUT>(uintptr_t(124)),packet);CHECK(_state.MouseWheel==-2.25f);
 _state.MenuVisible=false;UpdatePollingWheelHookLocked();CHECK(!_state.PollingWheelHook);
 device.dwFlags=RIDEV_REMOVE;device.hwndTarget=nullptr;CHECK(RegisterRawInputDevices(&device,1,sizeof(device)));
 DestroyWindow(other);DestroyWindow(_state.InputHwnd);
 std::puts("PASS production observer: install/detach, PM_NOREMOVE dedup, fractional and horizontal wheel, foreign window, unfocused, hidden menu, raw-only selection, raw handle dedup");
}
'''
    (out / "observer-test.cpp").write_text(preamble + observer + raw + tests, encoding="utf-8")
    (out / "build-observer.cmd").write_text(
        f'@echo off\ncall "{args.vcvars}" >nul\ncl /nologo /EHsc /std:c++20 /W4 /WX observer-test.cpp user32.lib /Fe:observer-test.exe\n', encoding="utf-8")
    env = {key.upper(): value for key, value in os.environ.items()}
    subprocess.run(["cmd.exe", "/d", "/c", "build-observer.cmd"], cwd=out, env=env, check=True)
    subprocess.run([str(out / "observer-test.exe")], cwd=out, check=True)


if __name__ == "__main__":
    main()
