// A synthetic game front end: load D18 and obtain Vulkan from the native loader.
// D18 owns SL initialization and teardown; the probe supplies only test inputs.
#define NOMINMAX
#include <windows.h>
#include <sl.h>
#include <cstdio>
#include <filesystem>
#include <atomic>
#include "vulkan-fg-lifecycle-probe.h"

static LONG CALLBACK TraceCppException(EXCEPTION_POINTERS* exception)
{
    static std::atomic<unsigned> captured{0};
    const auto code=exception->ExceptionRecord->ExceptionCode;
    if((code!=0xe06d7363 && code!=EXCEPTION_ACCESS_VIOLATION) || captured.fetch_add(1)>=8)
        return EXCEPTION_CONTINUE_SEARCH;
    void* frames[32]{};
    const auto count=CaptureStackBackTrace(0,32,frames,nullptr);
    printf("route_probe_exception_stack_begin code=%lx\n",code);
    for(USHORT i=0;i<count;++i)
    {
        HMODULE module{}; wchar_t path[MAX_PATH]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)frames[i],&module);
        if(module)GetModuleFileNameW(module,path,MAX_PATH);
        printf("frame=%u module=%ls offset=%llx\n",unsigned(i),path,
            (unsigned long long)((uintptr_t)frames[i]-(uintptr_t)module));
    }
    puts("route_probe_cpp_exception_stack_end");
    return EXCEPTION_CONTINUE_SEARCH;
}

int wmain(int argc,wchar_t** argv)
{
    setvbuf(stdout,nullptr,_IONBF,0);
    if(argc!=4){puts("usage: route-probe CORE_DIRECTORY LOG_DIRECTORY --lifecycle|--device-only|--present|--present-visible");return 2;}
    const bool deviceOnly=wcscmp(argv[3],L"--device-only")==0;
    const bool visible=wcscmp(argv[3],L"--present-visible")==0;
    const bool present=visible||wcscmp(argv[3],L"--present")==0;
    if(!deviceOnly&&!present&&wcscmp(argv[3],L"--lifecycle")){puts("unknown stage");return 2;}
    const auto root=std::filesystem::absolute(argv[1]);
    const auto logs=std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(logs);
    SetCurrentDirectoryW(root.c_str());
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto cookie=AddDllDirectory(root.c_str());
    const auto core=LoadLibraryExW((root/L"dxgi.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    printf("load_d18=%d last_error=%lu\n",core!=nullptr,GetLastError());
    if(!core)return 3;
    const auto exceptionTrace=AddVectoredExceptionHandler(0,TraceCppException);
    const auto loader=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!loader){puts("native Vulkan loader unavailable");return 4;}
    const auto gipa=(PFN_vkGetInstanceProcAddr)GetProcAddress(loader,"vkGetInstanceProcAddr");
    const auto gdpa=(PFN_vkGetDeviceProcAddr)GetProcAddress(loader,"vkGetDeviceProcAddr");
    if(!gipa||!gdpa)return 5;
    const bool ok=ProbeLifecycle(nullptr,nullptr,deviceOnly,present,visible,gipa,gdpa);
    if(exceptionTrace)RemoveVectoredExceptionHandler(exceptionTrace);
    // D18 holds process-wide hook leases. Let ordinary process exit detach it;
    // never FreeLibrary a hooked core while Vulkan callbacks could remain cached.
    if(cookie)RemoveDllDirectory(cookie);
    printf("d18_route_probe_result=%s\n",ok?"pass":"fail");
    return ok?0:6;
}
