#pragma once
#include "MonitorStats.h"
#include <Windows.h>
#include <mutex>
#include <string>

namespace D18Monitor
{
inline double clockSeconds()
{
    LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f);
    return double(q.QuadPart) / double(f.QuadPart);
}
struct View { double fps = -1, base = -1, low = -1, gpu = -1, watts = -1, updated = 0; };
struct Shared
{
    std::mutex mutex;
    Samples base, output;
    View view;
    double requested = 0;
    bool running = false;
    bool enabled = false, fg = false;
    void* owner = nullptr;
    unsigned epoch = 0;
    std::string adapterName;
};
// One bounded process-lifetime store: the worker owns a module reference until it exits.
inline Shared& data() { static auto* value = new Shared; return *value; }
inline void begin(void* owner, bool enabled, bool fg, bool foreground)
{
    auto& d = data(); std::lock_guard lock(d.mutex);
    if (d.owner != owner || d.enabled != enabled || d.fg != fg || !foreground)
    {
        d.base.reset(); d.output.reset(); d.view = {}; ++d.epoch;
        d.owner = owner; d.enabled = enabled; d.fg = fg;
    }
}
inline void frame(bool output, unsigned count, bool valid, bool foreground)
{
    auto& d = data(); std::lock_guard lock(d.mutex);
    if (!foreground) { d.base.reset(); d.output.reset(); d.view = {}; return; }
    (output ? d.output : d.base).add(clockSeconds(), count, valid);
}
inline DWORD WINAPI worker(void* module)
{
    // Load only the installed driver library, never a game-local DLL.
    wchar_t systemPath[MAX_PATH] {};
    GetSystemDirectoryW(systemPath, MAX_PATH);
    const std::wstring path = std::wstring(systemPath) + L"\\nvml.dll";
    HMODULE nvml = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    using Init = int(*)(); using Count = int(*)(unsigned*);
    using Device = int(*)(unsigned, void**); using Name = int(*)(void*, char*, unsigned);
    struct Usage { unsigned gpu, memory; };
    using Util = int(*)(void*, Usage*); using Power = int(*)(void*, unsigned*);
    const auto init = nvml ? (Init)GetProcAddress(nvml, "nvmlInit_v2") : nullptr;
    const auto shutdown = nvml ? (Init)GetProcAddress(nvml, "nvmlShutdown") : nullptr;
    const auto count = nvml ? (Count)GetProcAddress(nvml, "nvmlDeviceGetCount_v2") : nullptr;
    const auto device = nvml ? (Device)GetProcAddress(nvml, "nvmlDeviceGetHandleByIndex_v2") : nullptr;
    const auto name = nvml ? (Name)GetProcAddress(nvml, "nvmlDeviceGetName") : nullptr;
    const auto util = nvml ? (Util)GetProcAddress(nvml, "nvmlDeviceGetUtilizationRates") : nullptr;
    const auto power = nvml ? (Power)GetProcAddress(nvml, "nvmlDeviceGetPowerUsage") : nullptr;
    const bool initialized = init && shutdown && init() == 0;
    auto& d = data();
    // Keep the large statistics copy on the heap, not on a rendering/worker stack.
    auto samples = new std::pair<Samples, Samples>;
    for (;;)
    {
        std::string adapter; unsigned epoch;
        {
            std::lock_guard lock(d.mutex);
            if (clockSeconds() - d.requested > 2) { d.running = false; break; }
            samples->first = d.base; samples->second = d.output; adapter = d.adapterName; epoch = d.epoch;
        }
        View v; const auto now = clockSeconds();
        v.base = samples->first.fps(now); v.low = samples->first.low(now); v.fps = samples->second.fps(now);
        // Refuse ambiguous multi-GPU selection. A name is only accepted if unique.
        void* selected = nullptr; unsigned matches = 0, n = 0;
        if (initialized && count && device && name && !adapter.empty() && count(&n) == 0)
            for (unsigned i = 0; i < n; ++i)
            {
                void* candidate = nullptr; char label[128] {};
                if (device(i, &candidate) == 0 && name(candidate, label, sizeof(label)) == 0 && adapter == label)
                { selected = candidate; ++matches; }
            }
        if (matches == 1)
        {
            Usage u {}; unsigned mw = 0;
            if (util && util(selected, &u) == 0 && u.gpu <= 100) v.gpu = u.gpu;
            if (power && power(selected, &mw) == 0) v.watts = mw / 1000.0;
        }
        v.updated = clockSeconds();
        { std::lock_guard lock(d.mutex); if (epoch == d.epoch) d.view = v; }
        Sleep(500);
    }
    delete samples;
    if (initialized) shutdown();
    if (nvml) FreeLibrary(nvml);
    FreeLibraryAndExitThread((HMODULE)module, 0);
}
inline View read(const std::string& adapter)
{
    auto& d = data(); std::lock_guard lock(d.mutex);
    const auto now = clockSeconds();
    d.requested = now; d.adapterName = adapter;
    if (!d.running)
    {
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&worker, &self))
        {
            d.running = true;
            HANDLE thread = CreateThread(nullptr, 0, worker, self, 0, nullptr);
            if (thread) CloseHandle(thread);
            else { d.running = false; FreeLibrary(self); }
        }
    }
    return now - d.view.updated <= 2 ? d.view : View {};
}
}
