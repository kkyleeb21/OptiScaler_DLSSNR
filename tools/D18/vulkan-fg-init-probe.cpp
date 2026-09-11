// Standalone process: initialize user-supplied Streamline and query requirements.
// Isolated Vulkan lifecycle and optional synthetic FG presentation. No game injection.
#define NOMINMAX
#include <windows.h>
#include <sl.h>
#include <cstdio>
#include <filesystem>
#include <atomic>
#include "vulkan-fg-lifecycle-probe.h"

int wmain(int argc,wchar_t** argv){
    setvbuf(stdout,nullptr,_IONBF,0);
    printf("probe_pid=%lu main_tid=%lu\n",GetCurrentProcessId(),GetCurrentThreadId());
    if(argc!=3&&argc!=4){puts("usage: vulkan-fg-init-probe runtime-directory log-directory [--lifecycle|--device-only|--present|--present-visible]");return 2;}
    const bool deviceOnly=argc==4&&wcscmp(argv[3],L"--device-only")==0;
    const bool visibleStage=argc==4&&wcscmp(argv[3],L"--present-visible")==0;
    const bool presentStage=visibleStage||(argc==4&&wcscmp(argv[3],L"--present")==0);
    if(argc==4&&!deviceOnly&&!presentStage&&wcscmp(argv[3],L"--lifecycle")){puts("unknown stage");return 2;}
    const auto runtime=std::filesystem::absolute(argv[1]);
    const auto logs=std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(logs);
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto cookie=AddDllDirectory(runtime.c_str());
    if(!cookie){puts("runtime search directory failed");return 3;}
    const auto module=LoadLibraryExW((runtime/L"sl.interposer.dll").c_str(),nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module){printf("load failed: %lu\n",GetLastError());return 4;}
    auto init=reinterpret_cast<PFun_slInit*>(GetProcAddress(module,"slInit"));
    auto shutdown=reinterpret_cast<PFun_slShutdown*>(GetProcAddress(module,"slShutdown"));
    auto requirements=reinterpret_cast<PFun_slGetFeatureRequirements*>(GetProcAddress(module,"slGetFeatureRequirements"));
    if(!init||!shutdown||!requirements){puts("required entry missing");return 5;}
    sl::Preferences prefs{};
    const wchar_t* paths[]={runtime.c_str()};
    const sl::Feature features[]={sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL};
    prefs.pathsToPlugins=paths;prefs.numPathsToPlugins=1;
    prefs.pathToLogsAndData=logs.c_str();
    prefs.featuresToLoad=features;prefs.numFeaturesToLoad=3;
    prefs.applicationId=0x0F71CA1E;
    prefs.renderAPI=sl::RenderAPI::eVulkan;
    prefs.flags|=sl::PreferenceFlags::eUseManualHooking;
    prefs.flags|=sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    prefs.flags&=~sl::PreferenceFlags::eAllowOTA;
    prefs.flags&=~sl::PreferenceFlags::eLoadDownloadedPlugins;
    prefs.logLevel=sl::LogLevel::eDefault;
    prefs.logMessageCallback=[](sl::LogType type,const char* msg){
        static std::atomic<unsigned> records{0};
        if(records.fetch_add(1)<1000)printf("SL[%u] %.2048s\n",unsigned(type),msg?msg:"");
    };
    const auto started=init(prefs,sl::kSDKVersion);
    printf("slInit result=%u api=Vulkan sdk=%u.%u.%u\n",unsigned(started),SL_VERSION_MAJOR,SL_VERSION_MINOR,SL_VERSION_PATCH);
    if(started!=sl::Result::eOk)return 6;
    sl::FeatureRequirements req{};
    const auto queried=requirements(sl::kFeatureDLSS_G,req);
    printf("requirements result=%u flags=%u graphics=%u compute=%u optical=%u\n",
        unsigned(queried),unsigned(req.flags),req.vkNumGraphicsQueuesRequired,req.vkNumComputeQueuesRequired,req.vkNumOpticalFlowQueuesRequired);
    if(queried==sl::Result::eOk){
        for(uint32_t i=0;i<req.vkNumInstanceExtensions;++i)printf("instance_extension=%s\n",req.vkInstanceExtensions[i]);
        for(uint32_t i=0;i<req.vkNumDeviceExtensions;++i)printf("device_extension=%s\n",req.vkDeviceExtensions[i]);
    }
    if(argc==4&&queried==sl::Result::eOk){
        const bool ok=ProbeLifecycle(module,shutdown,deviceOnly,presentStage,visibleStage);
        FreeLibrary(module);RemoveDllDirectory(cookie);return ok?0:8;
    }
    const auto stopped=shutdown();printf("slShutdown result=%u\n",unsigned(stopped));
    FreeLibrary(module);RemoveDllDirectory(cookie);
    return queried==sl::Result::eOk && stopped==sl::Result::eOk?0:7;
}
