#include "pch.h"

#include "Vulkan_Hooks.h"

#include <Util.h>
#include <Config.h>
#include <SysUtils.h>

#include <menu/menu_overlay_vk.h>
#include <proxies/KernelBase_Proxy.h>
#include <upscaler_time/UpscalerTime_Vk.h>

#include <misc/FrameLimit.h>
#include "Reflex_Hooks.h"

#include <spoofing/Vulkan_Spoofing.h>

#include <vulkan/vulkan.hpp>

#include <dlssnr/DlssNr_VkExtensions.h>
#include <dlssnr/D24VkDiagnostics.h>
#include <dlssnr/D24VkTracking.h>
#include <dlssnr/VkColourCapture.h>
#include <dlssnr/DlssNrFeature_Vk.h>

#include <detours/detours.h>
#include <misc/IdentifyGpu.h>

#include "Hook_Utils.h"
#include "VulkanHookBindings.h"
#include <framegen/VulkanFgSwapchain.h>
#include <framegen/VulkanFgResources.h>
#include <framegen/VulkanFgInputCapture.h>

// for menu rendering
static VkDevice _device = VK_NULL_HANDLE;
static VkInstance _instance = VK_NULL_HANDLE;
static VkPhysicalDevice _PD = VK_NULL_HANDLE;
static HWND _hwnd = nullptr;

static std::mutex _vkPresentMutex;

PFN_vkCreateDevice o_vkCreateDevice = nullptr;
static PFN_vkDestroyDevice o_vkDestroyDevice = nullptr;
static void VKAPI_CALL hkvkDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator)
{
    DlssNr::VkAudit::DestroyDevice(device);
    DlssNr::ShutdownDeviceVk(device);
    if(VulkanFg::SwapchainRoute::Owns(device)) VulkanFg::Resources::Clear();
    if(!VulkanFg::SwapchainRoute::BeforeDestroyDevice(device)) return;
    o_vkDestroyDevice(device, allocator);
}
PFN_vkCreateInstance o_vkCreateInstance = nullptr;
PFN_vkCreateWin32SurfaceKHR o_vkCreateWin32SurfaceKHR = nullptr;
PFN_vkQueuePresentKHR o_QueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR o_CreateSwapchainKHR = nullptr;
static PFN_vkGetInstanceProcAddr o_vkGetInstanceProcAddr = nullptr;
static PFN_vkGetDeviceProcAddr o_vkGetDeviceProcAddr = nullptr;

// Those aren't hooked, just grabbed for use
static PFN_vkGetPhysicalDeviceFeatures2 o_vkGetPhysicalDeviceFeatures2 = nullptr;
PFN_vkCreateSemaphore VulkanHooks::o_vkCreateSemaphore = nullptr;
PFN_vkSignalSemaphore VulkanHooks::o_vkSignalSemaphore = nullptr;
PFN_vkAntiLagUpdateAMD VulkanHooks::o_vkAntiLagUpdateAMD = nullptr;

// Forward declaration
static VkResult hkvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);

static std::array<VulkanHookBinding,6> _loaderBindings{};
static std::array<VulkanHookBinding,2> _deviceBindings{};
static std::mutex _vkHookMutationMutex;
static std::atomic<HMODULE> _ownedFgRuntime{nullptr};
static VkDevice _hookedDevice = VK_NULL_HANDLE;
static thread_local unsigned _runtimeSetupDepth=0;
VulkanHooks::RuntimeSetupScope::RuntimeSetupScope() { ++_runtimeSetupDepth; }
VulkanHooks::RuntimeSetupScope::~RuntimeSetupScope() { --_runtimeSetupDepth; }

static void HookDevice(VkDevice InDevice)
{
    std::lock_guard<std::mutex> hookLock(_vkHookMutationMutex);
    if (_deviceBindings[0].installed || _deviceBindings[1].installed || State::Instance().vulkanSkipHooks)
        return;

    LOG_FUNC();

    // Do not invoke the delay-loaded Vulkan import while holding this mutex:
    // resolving it may re-enter VulkanHooks::Hook through a loader notification.
    o_QueuePresentKHR = (PFN_vkQueuePresentKHR)o_vkGetDeviceProcAddr(InDevice,"vkQueuePresentKHR");
    o_CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)o_vkGetDeviceProcAddr(InDevice,"vkCreateSwapchainKHR");

    if (o_CreateSwapchainKHR)
    {
        LOG_DEBUG("Hooking VkDevice");

        std::array<VulkanHookBinding,2> requested{{
            {&(PVOID&)o_QueuePresentKHR,(PVOID)hkvkQueuePresentKHR,o_QueuePresentKHR!=nullptr},
            {&(PVOID&)o_CreateSwapchainKHR,(PVOID)hkvkCreateSwapchainKHR,o_CreateSwapchainKHR!=nullptr}}};
        const auto result=ChangeVulkanBindings(requested,true);
        if (!result)
        {
            LOG_ERROR("Failed to hook VkDevice at {}, error code: {:X}",result.stage,result.code);
        }
        else { _deviceBindings=requested; _hookedDevice=InDevice; }
    }
}

// Streamline retains these dispatch entries and uses them on worker threads.
// Resolve once to native trampolines; a temporary thread-local bypass is not
// sufficient for those later calls.
static PFN_vkVoidFunction NativeFgProc(PFN_vkVoidFunction proc)
{
    return (PFN_vkVoidFunction)ResolveVulkanOriginal(_deviceBindings,
        ResolveVulkanOriginal(_loaderBindings,(PVOID)proc));
}

static PFN_vkVoidFunction VKAPI_CALL NativeFgInstanceProc(VkInstance instance,const char* name);
static PFN_vkVoidFunction VKAPI_CALL NativeFgDeviceProc(VkDevice device,const char* name);
static VkResult VKAPI_CALL NativeFgCreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,
    const VkAllocationCallbacks* allocator,VkDevice* device)
{
    // This hook implementation owns one device dispatch table. Never let a
    // second device silently reuse another device's driver-specific trampolines.
    if (_hookedDevice != VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result=o_vkCreateDevice(physical,info,allocator,device);
    if(result!=VK_SUCCESS) return result;
    // Install BEFORE SL caches native present/create-swapchain addresses.
    // Installing after SL's vkCreateDevice returns would patch cached entries
    // underneath its asynchronous presenter and re-enter the game route.
    HookDevice(*device);
    if(!_deviceBindings[0].installed || !_deviceBindings[1].installed || _hookedDevice!=*device)
    {
        o_vkDestroyDevice(*device,allocator);
        *device=VK_NULL_HANDLE;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return result;
}

static PFN_vkVoidFunction NativeFgSpecial(const char* name,PFN_vkVoidFunction proc)
{
    if(!proc || !name) return proc;
    if(strcmp(name,"vkGetInstanceProcAddr")==0) return (PFN_vkVoidFunction)NativeFgInstanceProc;
    if(strcmp(name,"vkGetDeviceProcAddr")==0) return (PFN_vkVoidFunction)NativeFgDeviceProc;
    if(strcmp(name,"vkCreateDevice")==0) return (PFN_vkVoidFunction)NativeFgCreateDevice;
    return NativeFgProc(proc);
}

static PFN_vkVoidFunction VKAPI_CALL NativeFgInstanceProc(VkInstance instance,const char* name)
{
    return NativeFgSpecial(name,o_vkGetInstanceProcAddr(instance,name));
}
static PFN_vkVoidFunction VKAPI_CALL NativeFgDeviceProc(VkDevice device,const char* name)
{
    return NativeFgSpecial(name,o_vkGetDeviceProcAddr(device,name));
}

PFN_vkVoidFunction VulkanHooks::NativeInstanceProc(VkInstance instance,const char* name)
{
    if(name && strcmp(name,"vkGetInstanceProcAddr")==0) return (PFN_vkVoidFunction)NativeInstanceProc;
    if(name && strcmp(name,"vkGetDeviceProcAddr")==0) return (PFN_vkVoidFunction)NativeDeviceProc;
    return NativeFgProc(o_vkGetInstanceProcAddr(instance,name));
}
PFN_vkVoidFunction VulkanHooks::NativeDeviceProc(VkDevice device,const char* name)
{
    if(name && strcmp(name,"vkGetInstanceProcAddr")==0) return (PFN_vkVoidFunction)NativeInstanceProc;
    if(name && strcmp(name,"vkGetDeviceProcAddr")==0) return (PFN_vkVoidFunction)NativeDeviceProc;
    return NativeFgProc(o_vkGetDeviceProcAddr(device,name));
}

bool VulkanHooks::RegisterOwnedFgRuntime(HMODULE module)
{
    if(!module) return false;
    std::lock_guard<std::mutex> lock(_vkHookMutationMutex);
    if(_hookedDevice || !o_vkDestroyDevice ||
       !std::all_of(_loaderBindings.begin(),_loaderBindings.end(),[](const auto& b){return b.installed;}))
        return false;
    HMODULE expected=nullptr;
    return _ownedFgRuntime.compare_exchange_strong(expected,module) || expected==module;
}

void VulkanHooks::ReleaseOwnedFgRuntime(HMODULE module)
{
    if(module) _ownedFgRuntime.compare_exchange_strong(module,nullptr);
}

FARPROC VulkanHooks::ResolveOwnedFgExport(HMODULE module,const char* name,void* caller)
{
    const auto owner=_ownedFgRuntime.load();
    if(!owner || module!=vulkanModule || (uintptr_t)name<=0xffff)
        return nullptr;
    if(_runtimeSetupDepth)
    {
        // Plugins may probe temporary instances during slInit. These must not
        // reserve the game's instance/device route. Returned native resolver
        // functions remain valid on other threads after this setup scope ends.
        if(strcmp(name,"vkGetInstanceProcAddr")==0) return (FARPROC)NativeInstanceProc;
        if(strcmp(name,"vkGetDeviceProcAddr")==0) return (FARPROC)NativeDeviceProc;
        return (FARPROC)NativeFgProc((PFN_vkVoidFunction)KernelBaseProxy::GetProcAddress_()(module,name));
    }
    if(Util::GetCallerModule(caller)!=owner) return nullptr;
    const auto original=KernelBaseProxy::GetProcAddress_()(module,name);
    return (FARPROC)NativeFgSpecial(name,(PFN_vkVoidFunction)original);
}

FARPROC VulkanHooks::ResolveFgGameExport(HMODULE module,const char* name)
{
    if(!vulkanModule || module!=vulkanModule || (uintptr_t)name<=0xffff) return nullptr;
    const auto original=(PFN_vkVoidFunction)KernelBaseProxy::GetProcAddress_()(module,name);
    if(!original) return nullptr;
    if(auto resources=VulkanFg::Resources::Resolve(name,original)) return (FARPROC)resources;
    return (FARPROC)VulkanFg::SwapchainRoute::Resolve(name);
}

VALIDATE_HOOK(hkvkCreateWin32SurfaceKHR, PFN_vkCreateWin32SurfaceKHR)
static VkResult hkvkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
    LOG_FUNC();

    auto result = VulkanFg::SwapchainRoute::Owns(instance)
        ? VulkanFg::SwapchainRoute::CreateSurface(instance,pCreateInfo,pAllocator,pSurface)
        : o_vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);

    auto procHwnd = Util::GetProcessWindow();
    LOG_DEBUG("procHwnd: {0:X}, swapchain hwnd: {1:X}", (UINT64) procHwnd, (UINT64) pCreateInfo->hwnd);

    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    {
        MenuOverlayVk::DestroyVulkanObjects(false);

        _instance = instance;
        State::Instance().VulkanInstance = instance;
        LOG_DEBUG("_instance captured: {0:X}", (UINT64) _instance);
        _hwnd = pCreateInfo->hwnd;
        LOG_DEBUG("_hwnd captured: {0:X}", (UINT64) _hwnd);
    }

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkvkCreateInstance, PFN_vkCreateInstance)
static VkResult hkvkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                   VkInstance* pInstance)
{
    LOG_FUNC();

    VkInstanceCreateInfo localCreateInfo {};
    memcpy(&localCreateInfo, pCreateInfo, sizeof(VkInstanceCreateInfo));
    // Decide from the application's ORIGINAL extension list. Spoofing may add
    // extensions even to headless capability probes. Such probes must neither
    // initialize SL nor consume the single presentation route's lifetime.
    bool requestsSurface=false,requestsWin32Surface=false;
    for(uint32_t n=0;n<pCreateInfo->enabledExtensionCount;++n)
    {
        const auto name=pCreateInfo->ppEnabledExtensionNames[n];
        requestsSurface|=strcmp(name,"VK_KHR_surface")==0;
        requestsWin32Surface|=strcmp(name,"VK_KHR_win32_surface")==0;
    }
    const bool fgPresentationInstance=VulkanFg::SwapchainRoute::Requested() && requestsSurface && requestsWin32Surface;
    DlssNr::VkAudit::Write("event=instance_request api=%u surface=%d win32_surface=%d fg_route=%d app=%s", pCreateInfo->pApplicationInfo ?
        pCreateInfo->pApplicationInfo->apiVersion : VK_API_VERSION_1_0,requestsSurface,requestsWin32Surface,fgPresentationInstance,
        pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->pApplicationName ? pCreateInfo->pApplicationInfo->pApplicationName : "unknown");

    VulkanSpoofing::hkvkCreateInstance(&localCreateInfo, pAllocator, pInstance);

    VkResult result;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = fgPresentationInstance
            ? VulkanFg::SwapchainRoute::CreateInstance(&localCreateInfo,pAllocator,pInstance)
            : o_vkCreateInstance(&localCreateInfo, pAllocator, pInstance);
    }
    DlssNr::VkAudit::Write("event=instance_result fg_route=%d result=%d instance=%p",fgPresentationInstance,int(result),
        result==VK_SUCCESS?(void*)*pInstance:nullptr);

    if (result == VK_SUCCESS)
    {
        State::Instance().VulkanInstance = *pInstance;
        LOG_DEBUG("State::Instance().VulkanInstance captured: {0:X}", (UINT64) State::Instance().VulkanInstance);

#ifdef VULKAN_DEBUG_LAYER
        auto address = vkGetInstanceProcAddr(State::Instance().VulkanInstance, "vkCreateDebugUtilsMessengerEXT");
        auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) address;
        VkDebugUtilsMessengerEXT debugMessenger;
        vkCreateDebugUtilsMessengerEXT(State::Instance().VulkanInstance, &VulkanSpoofing::debugCreateInfo, nullptr,
                                       &debugMessenger);
#endif
    }

    // Disabled to prevent unnecessary object release
    // if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    //{
    //     MenuOverlayVk::DestroyVulkanObjects(false);
    // }

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkvkCreateDevice, PFN_vkCreateDevice)
static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    LOG_FUNC();

    VkDeviceCreateInfo localCreteInfo {};
    memcpy(&localCreteInfo, pCreateInfo, sizeof(VkDeviceCreateInfo));
    DlssNr::VkAudit::DeviceRequest("game", *pCreateInfo);

    // Check support for AntiLag before spoof
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceAntiLagFeaturesAMD antiLagFeatures = {};
    antiLagFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD;

    features2.pNext = &antiLagFeatures;

    if (o_vkGetPhysicalDeviceFeatures2)
    {
        o_vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
        State::Instance().vkAntiLagSupported = antiLagFeatures.antiLag != 0;
    }

    VulkanSpoofing::hkvkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice);
    DlssNr::VkAudit::StorageFeatureCopy nrStorageFeatures;
    if(DlssNr::VkAudit::NativeArmed() && !DlssNr::VkAudit::ReadFeatures(localCreteInfo).storageExtended &&
       features2.features.shaderStorageImageExtendedFormats)
        DlssNr::VkAudit::Write("event=storage_feature_enable success=%d",nrStorageFeatures.Enable(localCreteInfo));

    // Neural Rendering on Vulkan without a D3D12 bridge, or the reason it cannot be.
    //
    // The model needs two NVIDIA vendor extensions to load its kernels, a game never asks for them,
    // and a device's extension list cannot be changed after creation. This is the only moment it can
    // be arranged. Reported either way: if the answer is no, the log says so here rather than leaving
    // a create failure three layers down to be explained.
    //
    // Only appended when the feature is switched on, and only what the physical device already
    // offers -- asking for an extension a driver does not have makes vkCreateDevice fail and the game
    // not start.
    DlssNr::VkExt::Merged nrExtensions;

    if (DlssNr::VkAudit::NativeExecutionValidated && Config::Instance()->DlssNrEnabled.value_or_default())
    {
        const auto supported = DlssNr::VkExt::SupportedDeviceExtensions(
            o_vkGetInstanceProcAddr, State::Instance().VulkanInstance, physicalDevice);

        nrExtensions.names.assign(localCreteInfo.ppEnabledExtensionNames,
                                  localCreteInfo.ppEnabledExtensionNames + localCreteInfo.enabledExtensionCount);

        std::string present, added, missing;

        for (const char* requirement : DlssNr::VkExt::kDevice)
        {
            const char* want = DlssNr::VkExt::DeviceRequirement(requirement,
                localCreteInfo.ppEnabledExtensionNames, localCreteInfo.enabledExtensionCount);
            const bool already = DlssNr::VkExt::ListHas(localCreteInfo.ppEnabledExtensionNames,
                                                        localCreteInfo.enabledExtensionCount, want);

            if (already)
                present += std::string(present.empty() ? "" : ", ") + want;
            else if (!DlssNr::VkExt::Contains(supported, want))
                missing += std::string(missing.empty() ? "" : ", ") + want;
            else
            {
                nrExtensions.names.push_back(want);
                added += std::string(added.empty() ? "" : ", ") + want;
            }
        }

        LOG_INFO("DLSS-NR Vulkan: device offers {} extensions. game already enabled: [{}]. added here: "
                 "[{}]. NOT AVAILABLE: [{}]",
                 supported.size(), present.empty() ? "none" : present, added.empty() ? "none" : added,
                 missing.empty() ? "none" : missing);

        if (!missing.empty())
            LOG_WARN("DLSS-NR Vulkan: the native path is not possible on this device -- the model's kernels "
                     "cannot be loaded without the extensions listed as NOT AVAILABLE");

        if (!added.empty())
        {
            localCreteInfo.ppEnabledExtensionNames = nrExtensions.names.data();
            localCreteInfo.enabledExtensionCount = (uint32_t) nrExtensions.names.size();
        }
    }

    DlssNr::VkAudit::DeviceRequest("effective", localCreteInfo);
    if (DlssNr::VkAudit::Enabled())
    {
        const auto supported = DlssNr::VkExt::SupportedDeviceExtensions(
            o_vkGetInstanceProcAddr, State::Instance().VulkanInstance, physicalDevice);
        for (const char* requirement : DlssNr::VkExt::kDevice)
        {
            const char* name = DlssNr::VkExt::DeviceRequirement(requirement,
                localCreteInfo.ppEnabledExtensionNames, localCreteInfo.enabledExtensionCount);
            DlssNr::VkAudit::Write("event=required_extension name=%s supported=%d enabled=%d", name,
                DlssNr::VkExt::Contains(supported, name), DlssNr::VkExt::ListHas(
                    localCreteInfo.ppEnabledExtensionNames, localCreteInfo.enabledExtensionCount, name));
        }
        // Use the BDA feature node, not Vulkan12Features: games may request Vulkan 1.1.
        if (o_vkGetPhysicalDeviceFeatures2 && DlssNr::VkExt::Contains(supported, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
        {
            VkPhysicalDeviceBufferDeviceAddressFeatures bda { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
            VkPhysicalDeviceFeatures2 queried { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            queried.pNext = &bda;
            o_vkGetPhysicalDeviceFeatures2(physicalDevice, &queried);
            DlssNr::VkAudit::Write("event=supported_features bda=%d int64=%d", bda.bufferDeviceAddress, queried.features.shaderInt64);
        }
    }
    auto result = VulkanFg::SwapchainRoute::HasDevice(physicalDevice)
        ? VulkanFg::SwapchainRoute::CreateDevice(physicalDevice,&localCreteInfo,pAllocator,pDevice)
        : o_vkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice);
    DlssNr::VkAudit::Write("event=device_result result=%d native_armed=%d", int(result),DlssNr::VkAudit::NativeArmed());
    if (result == VK_SUCCESS) DlssNr::VkAudit::RegisterDevice(*pDevice, o_vkGetDeviceProcAddr,
        DlssNr::VkAudit::ReadFeatures(localCreteInfo).storageExtended);
    if (result == VK_SUCCESS && o_vkGetInstanceProcAddr)
    {
        auto getFamilies=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            o_vkGetInstanceProcAddr(State::Instance().VulkanInstance,"vkGetPhysicalDeviceQueueFamilyProperties"));
        if(getFamilies)
        {
            uint32_t count=0;
            getFamilies(physicalDevice,&count,nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            if(count) getFamilies(physicalDevice,&count,families.data());
            families.resize(count);
            DlssNr::VkAudit::RegisterQueueFamilies(*pDevice,std::move(families));
        }
    }

    if (Config::Instance()->DlssNrEnabled.value_or_default())
        LOG_INFO("DLSS-NR Vulkan: vkCreateDevice returned {} with {} extensions requested", (int) result,
                 localCreteInfo.enabledExtensionCount);

    if (result == VK_SUCCESS && Config::Instance()->OverlayMenu.value_or_default())
    {
        if (!State::Instance().vulkanSkipHooks)
        {
            // Disabled to prevent unnecessary object release
            // MenuOverlayVk::DestroyVulkanObjects(false);

            _PD = physicalDevice;
            LOG_DEBUG("_PD captured: {0:X}", (UINT64) _PD);
            _device = *pDevice;
            LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);
            HookDevice(_device);
        }

        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        VkPhysicalDeviceIDProperties idProps {};
        idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2 {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &idProps;

        vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

        if (idProps.deviceLUIDValid == VK_TRUE)
        {
            auto primaryGpu = IdentifyGpu::getPrimaryGpu();
            auto luid = (PLUID) idProps.deviceLUID;
            if (!IsEqualLUID(*luid, primaryGpu.luid))
                LOG_WARN("VkDevice created with non-primary GPU");
        }
    }

    if (State::Instance().vkAntiLagSupported)
    {
        if (result == VK_SUCCESS && o_vkGetDeviceProcAddr)
        {
            VulkanHooks::o_vkAntiLagUpdateAMD =
                (PFN_vkAntiLagUpdateAMD) o_vkGetDeviceProcAddr(*pDevice, "vkAntiLagUpdateAMD");
        }
        else
        {
            State::Instance().vkAntiLagSupported = false;
            LOG_WARN("Vulkan AntiLag can't be enabled");
        }
    }

#ifdef USE_QUEUE_SUBMIT_2_KHR
    if (result == VK_SUCCESS)
        hkvkGetDeviceProcAddr(*pDevice, "vkQueueSubmit2KHR");
#endif

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkvkQueuePresentKHR, PFN_vkQueuePresentKHR)
static VkResult hkvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    LOG_FUNC();

    // get upscaler time
    UpscalerTimeVk::ReadUpscalingTime(_device);

    // ??? TODO: if we are hooking dxvk's vulkan calls then this present call could be either coming from dxvk or from a
    // native vk game
    if (!IdentifyGpu::getPrimaryGpu().usesDxvk)
        State::Instance().swapchainApi = Vulkan;

    // Tick feature to let it know if it's frozen
    if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
        currentFeature->TickFrozenCheck();

    VkPresentInfoKHR localPresentInfo {};
    memcpy(&localPresentInfo, pPresentInfo, sizeof(VkPresentInfoKHR));

    // render menu if needed
    if (!MenuOverlayVk::QueuePresent(queue, &localPresentInfo))
    {
        LOG_ERROR("QueuePresent: false!");
        return VK_ERROR_OUT_OF_DATE_KHR;
    }

    ReflexHooks::update(false, true);

    // original call
    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};
    auto result = VulkanFg::SwapchainRoute::OwnsPresent(&localPresentInfo)
        ? VulkanFg::SwapchainRoute::Present(queue,&localPresentInfo)
        : o_QueuePresentKHR(queue, &localPresentInfo);
    DlssNr::VkAudit::ObserveFgPresent(queue,&localPresentInfo,result);
    VulkanFg::InputCapture::Present(queue,result);

    // Unsure about Vulkan Reflex fps limit and if that could be causing an issue here
    if (!State::Instance().reflexLimitsFps)
        FrameLimit::sleep(false);

    LOG_FUNC_RESULT(result);
    return result;
}

VALIDATE_HOOK(hkvkCreateSwapchainKHR, PFN_vkCreateSwapchainKHR)
static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    LOG_FUNC();

    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};
    VkResult result = VK_SUCCESS;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = VulkanFg::SwapchainRoute::Owns(device)
            ? VulkanFg::SwapchainRoute::CreateSwapchain(device,pCreateInfo,pAllocator,pSwapchain)
            : o_CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }

    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE &&
        !State::Instance().vulkanSkipHooks)
    {
        State::Instance().screenWidth = static_cast<float>(pCreateInfo->imageExtent.width);
        State::Instance().screenHeight = static_cast<float>(pCreateInfo->imageExtent.height);

        // The same question the DXGI side asks: what does one unit of this buffer mean?
        //
        // EXTENDED_SRGB_LINEAR is scRGB, 1.0 = 80 nits. HDR10_ST2084 is PQ, 1.0 = 10000 nits. Both
        // are absolute, so in either the white point is arithmetic rather than a reading -- which
        // matters most for the games that supply no exposure texture, since nothing else answers for
        // them. Logged, not yet used.
        {
            static VkColorSpaceKHR lastSpace = (VkColorSpaceKHR) -1;
            DlssNr::ColourCapture::displaySpace = int(pCreateInfo->imageColorSpace);

            if (pCreateInfo->imageColorSpace != lastSpace)
            {
                lastSpace = pCreateInfo->imageColorSpace;

                const char* name = "other";
                const char* meaning = "relative -- no scale to be had";

                switch (pCreateInfo->imageColorSpace)
                {
                case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
                    name = "scRGB (extended sRGB, linear)";
                    meaning = "absolute: 1.0 = 80 nits, so 203-nit paper white = 2.5375";
                    break;
                case VK_COLOR_SPACE_HDR10_ST2084_EXT:
                    name = "PQ / ST.2084 (HDR10)";
                    meaning = "absolute: 1.0 = 10000 nits, so 203-nit paper white = 0.0203";
                    break;
                case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
                    name = "sRGB (SDR)";
                    break;
                case VK_COLOR_SPACE_HDR10_HLG_EXT:
                    name = "HLG";
                    break;
                default:
                    break;
                }

                LOG_INFO("DLSS-NR: swapchain colour space {} -- {} ({}), format {}",
                         (int) pCreateInfo->imageColorSpace, name, meaning, (int) pCreateInfo->imageFormat);
            }
        }

        LOG_DEBUG("if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && pSwapchain != "
                  "VK_NULL_HANDLE)");

        _device = device;
        LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);

        MenuOverlayVk::CreateSwapchain(device, _PD, _instance, _hwnd, pCreateInfo, pAllocator, pSwapchain);
    }

    LOG_FUNC_RESULT(result);
    return result;
}

VALIDATE_HOOK(hkvkGetInstanceProcAddr, PFN_vkGetInstanceProcAddr)
PFN_vkVoidFunction hkvkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    auto orgFunc = o_vkGetInstanceProcAddr(instance, pName);

    if (orgFunc == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    if(auto resources=VulkanFg::Resources::Resolve(pName,orgFunc)) return resources;
    if(auto route=VulkanFg::SwapchainRoute::Resolve(pName)) return route;

    auto procName = std::string(pName);
    if (procName == "vkDestroyDevice" && o_vkDestroyDevice)
        return (PFN_vkVoidFunction) hkvkDestroyDevice;

    if (procName == std::string("vkCreateInstance"))
    {
        if (o_vkCreateInstance == nullptr)
            o_vkCreateInstance = (PFN_vkCreateInstance) orgFunc;

        LOG_DEBUG("vkCreateInstance");
        return (PFN_vkVoidFunction) hkvkCreateInstance;
    }
    else if (procName == std::string("vkCreateDevice"))
    {
        if (o_vkCreateDevice == nullptr)
            o_vkCreateDevice = (PFN_vkCreateDevice) orgFunc;

        LOG_DEBUG("vkCreateDevice");
        return (PFN_vkVoidFunction) hkvkCreateDevice;
    }

    auto result = VulkanSpoofing::hkvkGetInstanceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    return orgFunc;
}

VALIDATE_HOOK(hkvkGetDeviceProcAddr, PFN_vkGetDeviceProcAddr)
PFN_vkVoidFunction hkvkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    auto orgFunc = o_vkGetDeviceProcAddr(device, pName);

    if (orgFunc == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    if(auto resources=VulkanFg::Resources::Resolve(pName,orgFunc)) return resources;
    if(auto route=VulkanFg::SwapchainRoute::Resolve(pName)) return route;

    auto procName = std::string(pName);
    if (procName == "vkDestroyDevice" && o_vkDestroyDevice)
        return (PFN_vkVoidFunction) hkvkDestroyDevice;

    if (procName == std::string("vkCreateInstance"))
    {
        if (o_vkCreateInstance == nullptr)
            o_vkCreateInstance = (PFN_vkCreateInstance) orgFunc;

        LOG_DEBUG("vkCreateInstance");
        return (PFN_vkVoidFunction) hkvkCreateInstance;
    }
    else if (procName == std::string("vkCreateDevice"))
    {
        if (o_vkCreateDevice == nullptr)
            o_vkCreateDevice = (PFN_vkCreateDevice) orgFunc;

        LOG_DEBUG("vkCreateDevice");
        return (PFN_vkVoidFunction) hkvkCreateDevice;
    }

    auto result = VulkanSpoofing::hkvkGetDeviceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    return orgFunc;
}

void VulkanHooks::Hook(HMODULE vulkan1)
{
    if (vulkanModule == nullptr)
        vulkanModule = vulkan1;

    VulkanSpoofing::HookForVulkanSpoofing(vulkan1);
    VulkanSpoofing::HookForVulkanExtensionSpoofing(vulkan1);
    VulkanSpoofing::HookForVulkanVRAMSpoofing(vulkan1);

    std::lock_guard<std::mutex> hookLock(_vkHookMutationMutex);
    if (std::any_of(_loaderBindings.begin(),_loaderBindings.end(),[](const auto& b){return b.installed;}))
        return;

    FARPROC address = nullptr;

    o_vkCreateDevice = (PFN_vkCreateDevice) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateDevice");
    o_vkCreateInstance = (PFN_vkCreateInstance) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateInstance");

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetInstanceProcAddr");
    o_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetDeviceProcAddr");
    o_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr) address;
    o_vkDestroyDevice = (PFN_vkDestroyDevice) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkDestroyDevice");

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateWin32SurfaceKHR");
    o_vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR) address;

    // address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCmdPipelineBarrier");
    // o_vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetPhysicalDeviceFeatures2");
    o_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateSemaphore");
    o_vkCreateSemaphore = (PFN_vkCreateSemaphore) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkSignalSemaphore");
    o_vkSignalSemaphore = (PFN_vkSignalSemaphore) address;

    std::array<VulkanHookBinding,6> requested{{
        {&(PVOID&)o_vkCreateDevice,(PVOID)hkvkCreateDevice,o_vkCreateDevice!=nullptr},
        {&(PVOID&)o_vkDestroyDevice,(PVOID)hkvkDestroyDevice,o_vkDestroyDevice!=nullptr},
        {&(PVOID&)o_vkGetInstanceProcAddr,(PVOID)hkvkGetInstanceProcAddr,o_vkGetInstanceProcAddr!=nullptr},
        {&(PVOID&)o_vkGetDeviceProcAddr,(PVOID)hkvkGetDeviceProcAddr,o_vkGetDeviceProcAddr!=nullptr},
        {&(PVOID&)o_vkCreateInstance,(PVOID)hkvkCreateInstance,o_vkCreateInstance!=nullptr},
        {&(PVOID&)o_vkCreateWin32SurfaceKHR,(PVOID)hkvkCreateWin32SurfaceKHR,o_vkCreateWin32SurfaceKHR!=nullptr}}};
    const auto result=ChangeVulkanBindings(requested,true);
    if(!result)LOG_ERROR("Failed to hook Vulkan at {}, error code: {:X}",result.stage,result.code);
    else _loaderBindings=requested;
}

void VulkanHooks::Unhook()
{
    std::lock_guard<std::mutex> hookLock(_vkHookMutationMutex);
    if(_ownedFgRuntime.load())
    {
        LOG_ERROR("Cannot detach Vulkan hooks while the owned FG runtime retains native dispatch entries");
        return;
    }
    std::array<VulkanHookBinding,8> all{};
    std::copy(_loaderBindings.begin(),_loaderBindings.end(),all.begin());
    std::copy(_deviceBindings.begin(),_deviceBindings.end(),all.begin()+_loaderBindings.size());
    if(std::none_of(all.begin(),all.end(),[](const auto& b){return b.installed;}))return;
    const auto result=ChangeVulkanBindings(all,false);
    if(!result){
        // Detours rolled back: retain the complete attachment inventory and trampolines.
        LOG_ERROR("Failed to unhook Vulkan at {}, error code: {:X}",result.stage,result.code);
        return;
    }
    _loaderBindings={};_deviceBindings={};
    _hookedDevice=VK_NULL_HANDLE;
    // Detach restores original addresses. Keep them callable for callbacks that
    // were already entered; null is not a safe detached-state sentinel.
}
