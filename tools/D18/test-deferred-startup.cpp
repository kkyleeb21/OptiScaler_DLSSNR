// Offline TLS/thread-start regression. Build with MSVC x64, /std:c++20 /EHsc /utf-8.
// Usage: test-deferred-startup.exe <core DLL> <test output root>
// Uses two fresh child processes; keeps artifacts and logs. No game files/runtime needed.
// LoadLibrary after CRT startup differs from a game's static import before CRT startup.
#include <windows.h>
#include <dxgi.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

static volatile LONG armed = 0;
static volatile LONG threadAttachCount = 0;

extern "C" void NTAPI CountThreadAttach(PVOID, DWORD reason, PVOID)
{
    if (reason == DLL_THREAD_ATTACH && InterlockedCompareExchange(&armed, 0, 0))
        InterlockedIncrement(&threadAttachCount);
}

#pragma section(".CRT$XLB", long, read)
extern "C" __declspec(allocate(".CRT$XLB")) const PIMAGE_TLS_CALLBACK deferredTestTls = CountThreadAttach;
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:deferredTestTls")

static std::string ReadLog(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static size_t Count(const std::string& text, const char* token)
{
    size_t count = 0, position = 0;
    while ((position = text.find(token, position)) != std::string::npos)
    {
        ++count;
        position += std::char_traits<char>::length(token);
    }
    return count;
}

static int Child(bool deferred)
{
    std::printf("MODE=%s TLS callback armed before LoadLibrary\n", deferred ? "true" : "false");
    std::fflush(stdout);
    InterlockedExchange(&threadAttachCount, 0);
    InterlockedExchange(&armed, 1);
    const HMODULE module = LoadLibraryW(L".\\dxgi.dll");
    if (!module)
    {
        std::printf("FAIL LoadLibrary Win32=%lu (environment/load failure; no sandbox bypass)\n", GetLastError());
        return 10;
    }
    Sleep(3000);
    const LONG starts = InterlockedCompareExchange(&threadAttachCount, 0, 0);
    InterlockedExchange(&armed, 0);
    std::printf("MODE=%s threads_during_load_and_3s=%ld\n", deferred ? "true" : "false", starts);
    std::fflush(stdout);
    if (!deferred)
    {
        std::printf("CONTROL %s\n", starts > 0 ? "PASS: positive thread-start sensitivity" :
                    "INCONCLUSIVE: zero starts; worker creation may have failed in this environment");
        return starts > 0 ? 0 : 20;
    }
    if (starts != 0)
    {
        std::puts("FAIL: thread started before graphics export");
        return 11;
    }
    if (ReadLog(L"OptiScaler.log").find("Deferred startup initialization") != std::string::npos)
    {
        std::puts("FAIL: initialization happened before graphics export");
        return 12;
    }
    using FactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    const auto create = reinterpret_cast<FactoryFn>(GetProcAddress(module, "CreateDXGIFactory1"));
    if (!create)
    {
        std::printf("FAIL GetProcAddress Win32=%lu\n", GetLastError());
        return 13;
    }
    // Call twice in this same test to verify the once-only completion marker.
    for (int call = 1; call <= 2; ++call)
    {
        IDXGIFactory1* factory = nullptr;
        const HRESULT hr = create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
        std::printf("CreateDXGIFactory1 call=%d HRESULT=0x%08lX factory=%s\n",
                    call, static_cast<unsigned long>(hr), factory ? "non-null" : "null");
        std::fflush(stdout);
        if (FAILED(hr) || !factory)
        {
            std::puts("FAIL: graphics entry failed; inspect runtime/sandbox error in logs");
            return 14;
        }
        factory->Release();
    }
    Sleep(300); // Allow the async logger to drain its queued completion/flush message.
    const auto log = ReadLog(L"OptiScaler.log");
    const size_t triggered = Count(log, "Deferred startup initialization triggered by CreateDXGIFactory1");
    const size_t completed = Count(log, "Deferred startup initialization complete: CreateDXGIFactory1");
    const bool release040 = log.find("db8776a") != std::string::npos;
    const bool initDone = log.find("Init done") != std::string::npos;
    std::printf("trigger_markers=%zu completion_markers=%zu init_done=%d release_040=%d\n",
                triggered, completed, initDone, release040);
    if (triggered != 1 || completed != 1 || !release040 || !initDone)
    {
        std::puts("FAIL: missing/repeated full-initialization evidence");
        return 15;
    }
    std::puts("PASS: zero pre-export starts, factory success, full initialization exactly once");
    // Do not FreeLibrary while original initialization's detached workers may still execute.
    // Normal process exit terminates the workers before DLL_PROCESS_DETACH.
    return 0;
}

static int RunChild(const std::filesystem::path& core, const std::filesystem::path& root,
                    const std::filesystem::path& self, bool deferred)
{
    const auto directory = root / (deferred ? L"true" : L"false");
    std::filesystem::create_directories(directory);
    const auto host = directory / L"test-deferred-startup.exe";
    std::filesystem::copy_file(self, host);
    std::filesystem::copy_file(core, directory / L"dxgi.dll");
    std::ofstream ini(directory / L"OptiScaler.ini");
    ini << "[Hooks]\nDeferredStartup=" << (deferred ? "true" : "false")
        << "\n[Log]\nLogToFile=true\nLogLevel=1\nLogAsync=true\nLogAsyncThreads=1\n"
           "LogFileName=OptiScaler.log\nOpenConsole=false\nLogToConsole=false\n"
           "[Hotfix]\nCheckForUpdate=false\n[Plugins]\nLoadAsiPlugins=false\n"
           "LoadReShade=false\nLoadSpecialK=false\n";
    ini.close();
    // Async logging is intentionally enabled in both cases to expose premature workers.
    // Update checks disabled to keep this explicitly offline. No NVIDIA/runtime DLLs copied.
    std::wstring command = L"\"" + host.wstring() + L"\" --child " + (deferred ? L"true" : L"false");
    STARTUPINFOW startup {sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process {};
    std::printf("CASE directory=%s\n", directory.string().c_str());
    std::fflush(stdout);
    if (!CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, directory.c_str(), &startup, &process))
    {
        std::printf("FAIL CreateProcess Win32=%lu; stopping\n", GetLastError());
        return 30;
    }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 90000);
    DWORD code = 31;
    if (wait == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &code);
    else
    {
        std::printf("FAIL child wait=%lu Win32=%lu; terminating only this test child\n", wait, GetLastError());
        TerminateProcess(process.hProcess, code);
        WaitForSingleObject(process.hProcess, 5000);
    }
    CloseHandle(process.hProcess);
    std::printf("CASE mode=%s exit=%lu\n", deferred ? "true" : "false", code);
    std::fflush(stdout);
    return static_cast<int>(code);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 3 && std::wstring(argv[1]) == L"--child")
        return Child(std::wstring(argv[2]) == L"true");
    if (argc != 3)
    {
        std::puts("Usage: test-deferred-startup.exe <core DLL> <test output root>");
        return 2;
    }
    try
    {
        wchar_t selfPath[32768];
        if (!GetModuleFileNameW(nullptr, selfPath, 32768))
            return 3;
        const auto root = std::filesystem::absolute(argv[2]) /
                          (L"run-" + std::to_wstring(GetCurrentProcessId()));
        const auto core = std::filesystem::absolute(argv[1]);
        const int result = RunChild(core, root, selfPath, true);
        if (result != 0)
            return result; // Stop at a possible sandbox/environment block, no bypass.
        const int control = RunChild(core, root, selfPath, false);
        std::puts("LIMITATION: runtime LoadLibrary timing differs from static imports before EXE CRT entry.");
        return control;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL filesystem/setup: %s\n", error.what());
        return 4;
    }
}
