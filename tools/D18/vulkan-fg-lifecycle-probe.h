#pragma once
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <sl_dlss_g.h>
#include <nvsdk_ngx_vk.h>
#include "vulkan-fg-present-probe.h"

// Visible synthetic presentation is an explicit interactive opt-in.
inline bool ProbeLifecycle(HMODULE module,PFun_slShutdown* shutdown,bool deviceOnly=false,bool presentStage=false,bool visibleStage=false,
    PFN_vkGetInstanceProcAddr gameGipa=nullptr,PFN_vkGetDeviceProcAddr gameGdpa=nullptr){
    const bool managed=gameGipa&&gameGdpa;
    auto gipa=managed?gameGipa:reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(module,"vkGetInstanceProcAddr"));
    auto gdpa=managed?gameGdpa:reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(module,"vkGetDeviceProcAddr"));
    VkInstance instance{};VkDevice device{};VkSurfaceKHR surface{};VkSwapchainKHR swapchain{};
    HWND window{};bool success=false;
    auto check=[](VkResult r,const char* step){printf("%s result=%d\n",step,int(r));if(r!=VK_SUCCESS)throw std::runtime_error(step);};
    auto ip=[&](const char* n){auto p=gipa(instance,n);if(!p)throw std::runtime_error(n);return p;};
    auto dp=[&](const char* n){auto p=gdpa(device,n);if(!p)throw std::runtime_error(n);return p;};
    try{
        if(!gipa||!gdpa)throw std::runtime_error("Vulkan proxy exports");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="D18 Vulkan lifecycle probe";app.apiVersion=VK_API_VERSION_1_3;
        const char* extensions[]={"VK_KHR_surface","VK_KHR_win32_surface"};
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.enabledExtensionCount=2;ci.ppEnabledExtensionNames=extensions;
        if(managed)
        {
            VkInstance probe{};
            VkInstanceCreateInfo headless{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};headless.pApplicationInfo=&app;
            check(reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"))(&headless,nullptr,&probe),"headless_probe_create");
            reinterpret_cast<PFN_vkDestroyInstance>(gipa(probe,"vkDestroyInstance"))(probe,nullptr);
            puts("headless_probe_destroyed_before_game_instance");
        }
        check(reinterpret_cast<PFN_vkCreateInstance>(ip("vkCreateInstance"))(&ci,nullptr,&instance),"create_instance_proxy");
        WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"D18VkFgProbe";
        if(!RegisterClassW(&wc))throw std::runtime_error("register hidden window");
        window=CreateWindowExW(0,wc.lpszClassName,L"D18 probe",WS_OVERLAPPEDWINDOW,0,0,presentStage?1320:160,presentStage?800:120,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window)throw std::runtime_error("create hidden window");
        if(visibleStage){
            ShowWindow(window,SW_SHOWNORMAL);
            // STARTUPINFO may override the first ShowWindow (the shared runner
            // starts helpers hidden). Honor this explicit interactive stage.
            if(!IsWindowVisible(window))ShowWindow(window,SW_SHOWNORMAL);
            UpdateWindow(window);SetForegroundWindow(window);
            // Explicit --present-visible test only. Foreground lock can reject
            // SetForegroundWindow for a shell-launched synthetic process.
            // Temporarily join input queues; always detach before testing.
            if(GetForegroundWindow()!=window)
            {
                const DWORD current=GetCurrentThreadId();
                const DWORD foreground=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
                const bool attached=foreground && foreground!=current && AttachThreadInput(current,foreground,TRUE);
                BringWindowToTop(window);SetForegroundWindow(window);SetFocus(window);
                if(attached)AttachThreadInput(current,foreground,FALSE);
            }
        }
        printf("window_visible=%d foreground=%d\n",IsWindowVisible(window)!=0,GetForegroundWindow()==window);
        VkWin32SurfaceCreateInfoKHR sc{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};sc.hinstance=wc.hInstance;sc.hwnd=window;
        check(reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(ip("vkCreateWin32SurfaceKHR"))(instance,&sc,nullptr,&surface),"create_surface_proxy");
        auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(ip("vkEnumeratePhysicalDevices"));uint32_t count=0;
        check(enumerate(instance,&count,nullptr),"count_devices");std::vector<VkPhysicalDevice> devices(count);check(enumerate(instance,&count,devices.data()),"enumerate_devices");
        VkPhysicalDevice physical{};uint32_t family=UINT32_MAX;
        for(auto candidate:devices){
            VkPhysicalDeviceProperties props{};reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(ip("vkGetPhysicalDeviceProperties"))(candidate,&props);
            if(props.vendorID!=0x10de)continue;
            uint32_t n=0;auto families=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(ip("vkGetPhysicalDeviceQueueFamilyProperties"));families(candidate,&n,nullptr);
            std::vector<VkQueueFamilyProperties> queues(n);families(candidate,&n,queues.data());
            for(uint32_t i=0;i<n;++i){VkBool32 present=0;check(reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(ip("vkGetPhysicalDeviceSurfaceSupportKHR"))(candidate,i,surface,&present),"surface_support");
                if(present&&queues[i].queueCount&&(queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){physical=candidate;family=i;break;}}
            if(physical){printf("device=%s graphics_family=%u\n",props.deviceName,family);break;}
        }
        if(!physical)throw std::runtime_error("NVIDIA graphics/present queue unavailable");
        float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
        const char* de[]={"VK_KHR_swapchain"};VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qi;dc.enabledExtensionCount=1;dc.ppEnabledExtensionNames=de;
        check(reinterpret_cast<PFN_vkCreateDevice>(ip("vkCreateDevice"))(physical,&dc,nullptr,&device),"create_device_proxy");
        if(managed)module=GetModuleHandleW(L"sl.interposer.dll");
        if(managed)
        {
            // The FG host used to omit the game's independent SR loader path.
            // A loaded SL/NGX runtime must not steal the subsequent game's input.
            const auto sr=LoadLibraryW(L"nvngx.dll");
            const auto core=GetModuleHandleW(L"dxgi.dll");
            printf("game_sr_loader_after_fg module=%p core=%p routed_to_d18=%d\n",sr,core,sr&&sr==core);
            if(!sr||sr!=core)throw std::runtime_error("game SR loader bypassed D18 after FG initialization");
            using Init=NVSDK_NGX_Result (*)(unsigned long long,const wchar_t*,VkInstance,VkPhysicalDevice,VkDevice,
                PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,NVSDK_NGX_Version,const NVSDK_NGX_Parameter*);
            const auto init=reinterpret_cast<Init>(GetProcAddress(sr,"NVSDK_NGX_VULKAN_Init_Ext2"));
            if(!init)throw std::runtime_error("game SR init export missing");
            const auto initialized=init(0x0F71CA1E,L".",instance,physical,device,gipa,gdpa,NVSDK_NGX_Version_API,nullptr);
            printf("game_sr_init_after_fg result=%u\n",unsigned(initialized));
            if(NVSDK_NGX_FAILED(initialized))throw std::runtime_error("game SR initialization failed after FG");
            using Parameters=NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter**);
            using Create=NVSDK_NGX_Result (*)(VkDevice,VkCommandBuffer,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
            using Release=NVSDK_NGX_Result (*)(NVSDK_NGX_Handle*);
            using DestroyParameters=NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter*);
            auto getParameters=reinterpret_cast<Parameters>(GetProcAddress(sr,"NVSDK_NGX_VULKAN_GetCapabilityParameters"));
            auto create=reinterpret_cast<Create>(GetProcAddress(sr,"NVSDK_NGX_VULKAN_CreateFeature1"));
            auto release=reinterpret_cast<Release>(GetProcAddress(sr,"NVSDK_NGX_VULKAN_ReleaseFeature"));
            auto destroyParameters=reinterpret_cast<DestroyParameters>(GetProcAddress(sr,"NVSDK_NGX_VULKAN_DestroyParameters"));
            if(!getParameters||!create||!release||!destroyParameters)throw std::runtime_error("SR feature exports missing");
            NVSDK_NGX_Parameter* parameters{};
            const auto parameterResult=getParameters(&parameters);
            printf("game_sr_parameters result=%u\n",unsigned(parameterResult));
            if(NVSDK_NGX_FAILED(parameterResult)||!parameters)throw std::runtime_error("SR parameters unavailable");
            VkCommandPool pool{};VkCommandBuffer command{};NVSDK_NGX_Handle* feature{};
            const auto destroyPool=reinterpret_cast<PFN_vkDestroyCommandPool>(dp("vkDestroyCommandPool"));
            auto clear=[&]{if(feature){release(feature);feature=nullptr;}if(pool){destroyPool(device,pool,nullptr);pool={};}if(parameters){destroyParameters(parameters);parameters=nullptr;}};
            try{
                VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family;
                check(reinterpret_cast<PFN_vkCreateCommandPool>(dp("vkCreateCommandPool"))(device,&poolInfo,nullptr,&pool),"SR create pool");
                VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.commandPool=pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
                check(reinterpret_cast<PFN_vkAllocateCommandBuffers>(dp("vkAllocateCommandBuffers"))(device,&allocation,&command),"SR allocate command");
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                check(reinterpret_cast<PFN_vkBeginCommandBuffer>(dp("vkBeginCommandBuffer"))(command,&begin),"SR begin command");
                parameters->Set(NVSDK_NGX_Parameter_Width,1920u);parameters->Set(NVSDK_NGX_Parameter_Height,1080u);
                parameters->Set(NVSDK_NGX_Parameter_OutWidth,3840u);parameters->Set(NVSDK_NGX_Parameter_OutHeight,2160u);
                parameters->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(NVSDK_NGX_PerfQuality_Value_MaxPerf));
                parameters->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted));
                const auto created=create(device,command,NVSDK_NGX_Feature_SuperSampling,parameters,&feature);
                printf("game_sr_feature_after_fg result=%u handle=%p\n",unsigned(created),feature);
                check(reinterpret_cast<PFN_vkEndCommandBuffer>(dp("vkEndCommandBuffer"))(command),"SR end command");
                VkQueue queue{};reinterpret_cast<PFN_vkGetDeviceQueue>(dp("vkGetDeviceQueue"))(device,family,0,&queue);
                VkSubmitInfo submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};submission.commandBufferCount=1;submission.pCommandBuffers=&command;
                check(reinterpret_cast<PFN_vkQueueSubmit>(dp("vkQueueSubmit"))(queue,1,&submission,VK_NULL_HANDLE),"SR submit initialization");
                check(reinterpret_cast<PFN_vkDeviceWaitIdle>(dp("vkDeviceWaitIdle"))(device),"SR initialization completion");
                if(NVSDK_NGX_FAILED(created)||!feature)throw std::runtime_error("SR feature creation failed after FG");
            }catch(...){clear();throw;}
            clear();
            // OptiScaler's redirected handle does not grant an unload lease.
        }
        if(!module)throw std::runtime_error("D18 did not initialize its interposer");
        auto featureFunction=reinterpret_cast<PFun_slGetFeatureFunction*>(GetProcAddress(module,"slGetFeatureFunction"));
        void* setAddress=nullptr;
        if(!featureFunction||featureFunction(sl::kFeatureDLSS_G,"slDLSSGSetOptions",setAddress)!=sl::Result::eOk||!setAddress)
            throw std::runtime_error("FG options unavailable after device creation");
        sl::DLSSGOptions off{};off.mode=sl::DLSSGMode::eOff;
        const auto disabled=reinterpret_cast<PFun_slDLSSGSetOptions*>(setAddress)(sl::ViewportHandle(0),off);
        printf("explicit_fg_off=%u\n",unsigned(disabled));
        if(disabled!=sl::Result::eOk)throw std::runtime_error("explicit FG off failed");
        void* stateAddress=nullptr;
        if(featureFunction(sl::kFeatureDLSS_G,"slDLSSGGetState",stateAddress)!=sl::Result::eOk||!stateAddress)
            throw std::runtime_error("FG capability query unavailable");
        sl::DLSSGState fgState{};
        const auto queriedState=reinterpret_cast<PFun_slDLSSGGetState*>(stateAddress)(sl::ViewportHandle(0),fgState,nullptr);
        printf("fg_capability result=%u max_generated=%u status=%u presented=%u\n",
            unsigned(queriedState),fgState.numFramesToGenerateMax,unsigned(fgState.status),fgState.numFramesActuallyPresented);
        // Capability is not proof of generated/presented frames. Do not enable FG
        // without same-frame resources and constants in this empty host.
        if(queriedState!=sl::Result::eOk)throw std::runtime_error("FG capability query failed");
        VkSurfaceCapabilitiesKHR caps{};check(reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(ip("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))(physical,surface,&caps),"surface_caps");
        uint32_t nf=0;auto getFormats=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(ip("vkGetPhysicalDeviceSurfaceFormatsKHR"));check(getFormats(physical,surface,&nf,nullptr),"format_count");
        std::vector<VkSurfaceFormatKHR> formats(nf);check(getFormats(physical,surface,&nf,formats.data()),"formats");if(formats.empty())throw std::runtime_error("no formats");
        if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))throw std::runtime_error("color attachment unsupported");
        for(int cycle=0;cycle<(deviceOnly?0:(presentStage?1:3));++cycle){
            if(!presentStage){
                const int sizes[][2]={{640,400},{1280,800},{960,600}};
                SetWindowPos(window,nullptr,0,0,sizes[cycle][0],sizes[cycle][1],SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                check(reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(ip("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))(physical,surface,&caps),"resized_surface_caps");
            }
            VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};info.surface=surface;
            info.minImageCount=std::max(2u,caps.minImageCount);if(caps.maxImageCount)info.minImageCount=std::min(info.minImageCount,caps.maxImageCount);
            info.imageFormat=formats[0].format==VK_FORMAT_UNDEFINED?VK_FORMAT_B8G8R8A8_UNORM:formats[0].format;info.imageColorSpace=formats[0].colorSpace;
            info.imageExtent=caps.currentExtent;if(info.imageExtent.width==UINT32_MAX)info.imageExtent={std::clamp(128u,caps.minImageExtent.width,caps.maxImageExtent.width),std::clamp(96u,caps.minImageExtent.height,caps.maxImageExtent.height)};
            info.imageArrayLayers=1;info.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;info.preTransform=caps.currentTransform;
            info.compositeAlpha=static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha&(~caps.supportedCompositeAlpha+1));info.presentMode=VK_PRESENT_MODE_FIFO_KHR;info.clipped=VK_TRUE;
            if(presentStage){
                if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("present probe requires transfer destination");
                info.imageUsage|=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                auto modes=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(ip("vkGetPhysicalDeviceSurfacePresentModesKHR"));
                uint32_t modeCount=0;check(modes(physical,surface,&modeCount,nullptr),"present mode count");
                std::vector<VkPresentModeKHR> values(modeCount);check(modes(physical,surface,&modeCount,values.data()),"present modes");
                if(std::find(values.begin(),values.end(),VK_PRESENT_MODE_IMMEDIATE_KHR)==values.end())throw std::runtime_error("immediate present unavailable");
                info.presentMode=VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
            const auto previous=swapchain;info.oldSwapchain=previous;
            VkSwapchainKHR replacement{};
            check(reinterpret_cast<PFN_vkCreateSwapchainKHR>(dp("vkCreateSwapchainKHR"))(device,&info,nullptr,&replacement),"create_swapchain_proxy");
            swapchain=replacement;
            if(previous)reinterpret_cast<PFN_vkDestroySwapchainKHR>(dp("vkDestroySwapchainKHR"))(device,previous,nullptr);
            uint32_t images=0;check(reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(dp("vkGetSwapchainImagesKHR"))(device,swapchain,&images,nullptr),"get_swapchain_images_proxy");
            if(!images)throw std::runtime_error("empty swapchain");printf("cycle=%d images=%u extent=%ux%u old_destroyed=%d\n",cycle,images,info.imageExtent.width,info.imageExtent.height,previous!=VK_NULL_HANDLE);
            std::vector<VkImage> actualImages(images);
            check(reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(dp("vkGetSwapchainImagesKHR"))(device,swapchain,&images,actualImages.data()),"get_actual_images_after_old_destroy");
            if(presentStage)ProbePresent(module,gipa,gdpa,instance,physical,device,swapchain,family,info.imageExtent,fgState.numFramesToGenerateMax,visibleStage?window:nullptr);
            check(reinterpret_cast<PFN_vkDeviceWaitIdle>(dp("vkDeviceWaitIdle"))(device),"device_idle_proxy");
        }
        success=true;
    }catch(const std::exception& e){printf("lifecycle_failed=%s\n",e.what());}
    puts("cleanup_idle_begin");
    if(device&&gdpa){auto idle=reinterpret_cast<PFN_vkDeviceWaitIdle>(gdpa(device,"vkDeviceWaitIdle"));if(idle)idle(device);
        auto destroy=reinterpret_cast<PFN_vkDestroySwapchainKHR>(gdpa(device,"vkDestroySwapchainKHR"));if(swapchain&&destroy)destroy(device,swapchain,nullptr);}
    // The surface proxy accesses SL's plugin manager. Release it while that
    // manager is still alive; keep device/instance alive for NGX shutdown.
    if(surface){reinterpret_cast<PFN_vkDestroySurfaceKHR>(gipa(instance,"vkDestroySurfaceKHR"))(instance,surface,nullptr);surface={};}
    puts("shutdown_begin");
    const auto stopped=managed?sl::Result::eOk:shutdown();
    if(!managed)printf("slShutdown result=%u\n",unsigned(stopped));
    if(device)reinterpret_cast<PFN_vkDestroyDevice>(gdpa(device,"vkDestroyDevice"))(device,nullptr);
    if(instance)reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"))(instance,nullptr);
    if(managed)puts("managed_device_and_instance_cleanup_returned; verify D18 slShutdown result in core log");
    if(window)DestroyWindow(window);UnregisterClassW(L"D18VkFgProbe",GetModuleHandleW(nullptr));
    return success&&stopped==sl::Result::eOk;
}
