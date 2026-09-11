#pragma once
#include <array>
#include <chrono>
#include <proxies/Streamline_Proxy.h>
#include <hooks/Vulkan_Hooks.h>
#include <menu/menu_overlay_vk.h>
#include "VulkanFgFrame.h"

// Experimental single-device route. Vulkan object handles remain game-owned;
// only swapchain operations use SL's presentation proxy. Native runtime calls
// bypass this class through the registered native dispatch resolver.
namespace VulkanFg
{
class SwapchainRoute
{
    inline static PFN_vkGetInstanceProcAddr gipa{};
    inline static PFN_vkGetDeviceProcAddr gdpa{};
    inline static VkInstance instance{};
    inline static VkDevice device{};
    inline static std::array<VkPhysicalDevice,16> physicalDevices{};
    inline static uint32_t physicalCount{};
    inline static std::array<VkSwapchainKHR,2> swapchains{};
    inline static bool live=false;

public:
    static bool Requested()
    {
        static const bool requested=Config::Instance()->FGVulkanExperimental.value_or_default();
        return requested; // Route ownership cannot change under live proxy handles.
    }
    static bool Owns(VkDevice value) { return live && device && value==device; }
    static bool Owns(VkInstance value) { return live && instance && value==instance; }
    static VkDevice Device() { return live ? device : VK_NULL_HANDLE; }
    static VkInstance Instance() { return live ? instance : VK_NULL_HANDLE; }
    static bool Contains(VkSwapchainKHR value)
    {
        return value && std::find(swapchains.begin(),swapchains.end(),value)!=swapchains.end();
    }
    static bool HasDevice(VkPhysicalDevice value)
    {
        return live && std::find(physicalDevices.begin(),physicalDevices.begin()+physicalCount,value)
            !=physicalDevices.begin()+physicalCount;
    }
    static VkResult CreateInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* allocator,VkInstance* result)
    {
        if(instance || !StreamlineProxy::PrepareVulkan()) return VK_ERROR_INITIALIZATION_FAILED;
        const auto module=StreamlineProxy::Module();
        gipa=(PFN_vkGetInstanceProcAddr)KernelBaseProxy::GetProcAddress_()(module,"vkGetInstanceProcAddr");
        gdpa=(PFN_vkGetDeviceProcAddr)KernelBaseProxy::GetProcAddress_()(module,"vkGetDeviceProcAddr");
        if(!gipa || !gdpa) { StreamlineProxy::ShutdownVulkan(); return VK_ERROR_INITIALIZATION_FAILED; }
        const auto create=(PFN_vkCreateInstance)gipa(nullptr,"vkCreateInstance");
        if(!create) { StreamlineProxy::ShutdownVulkan(); return VK_ERROR_INITIALIZATION_FAILED; }
        auto status=create(info,allocator,result);
        if(status!=VK_SUCCESS) { StreamlineProxy::ShutdownVulkan(); return status; }
        instance=*result; live=true;
        const auto enumerate=(PFN_vkEnumeratePhysicalDevices)gipa(instance,"vkEnumeratePhysicalDevices");
        physicalCount=static_cast<uint32_t>(physicalDevices.size());
        status=enumerate ? enumerate(instance,&physicalCount,physicalDevices.data()) : VK_ERROR_INITIALIZATION_FAILED;
        if(status!=VK_SUCCESS) physicalCount=0;
        return VK_SUCCESS;
    }
    static VkResult CreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,
        const VkAllocationCallbacks* allocator,VkDevice* result)
    {
        if(device || !HasDevice(physical)) return VK_ERROR_INITIALIZATION_FAILED;
        const auto create=(PFN_vkCreateDevice)gipa(instance,"vkCreateDevice");
        if(!create) return VK_ERROR_INITIALIZATION_FAILED;
        const auto status=create(physical,info,allocator,result);
        if(status!=VK_SUCCESS) return status;
        device=*result;
        // A binding failure leaves the proxy route alive but explicitly Off.
        // Destroying a successfully created device here would strand SL's state.
        if(!StreamlineProxy::CompleteVulkanDevice()) LOG_ERROR("Vulkan FG device created, but feature binding failed; FG remains Off");
        return VK_SUCCESS;
    }
    static VkResult CreateSurface(VkInstance value,const VkWin32SurfaceCreateInfoKHR* info,
        const VkAllocationCallbacks* allocator,VkSurfaceKHR* result)
    {
        return ((PFN_vkCreateWin32SurfaceKHR)gipa(value,"vkCreateWin32SurfaceKHR"))(value,info,allocator,result);
    }
    static VkResult CreateSwapchain(VkDevice value,const VkSwapchainCreateInfoKHR* info,
        const VkAllocationCallbacks* allocator,VkSwapchainKHR* result)
    {
        const auto slot=std::find(swapchains.begin(),swapchains.end(),VK_NULL_HANDLE);
        const bool any=std::any_of(swapchains.begin(),swapchains.end(),[](auto sc){return sc!=VK_NULL_HANDLE;});
        if(slot==swapchains.end() || (any && !Contains(info->oldSwapchain))) return VK_ERROR_INITIALIZATION_FAILED;
        const auto status=((PFN_vkCreateSwapchainKHR)gdpa(value,"vkCreateSwapchainKHR"))(value,info,allocator,result);
        if(status==VK_SUCCESS) *slot=*result;
        return status;
    }
    static VkResult Present(VkQueue queue,const VkPresentInfoKHR* info)
    {
        if(info->swapchainCount!=1 || !Contains(info->pSwapchains[0])) return VK_ERROR_OUT_OF_DATE_KHR;
        if(!info->waitSemaphoreCount)Frame::InvalidateFrame();
        const bool audit=Config::Instance()->DlssNrDiagnostics.value_or_default()!=0;
        using Clock=std::chrono::steady_clock;
        const auto start=audit?Clock::now():Clock::time_point{};
        Frame::BeforePresent(queue);
        const auto begin=audit?Clock::now():Clock::time_point{};
        const auto result=((PFN_vkQueuePresentKHR)gdpa(device,"vkQueuePresentKHR"))(queue,info);
        const auto end=audit?Clock::now():Clock::time_point{};
        Frame::AfterPresent(result);
        if(audit){const auto done=Clock::now();auto us=[](auto value){return std::chrono::duration<double,std::micro>(value).count();};
            Frame::LogPacing(us(begin-start),us(end-begin),us(done-end),result);}
        return result;
    }
    static bool OwnsPresent(const VkPresentInfoKHR* info)
    {
        if(!live || !info || !info->pSwapchains) return false;
        for(uint32_t i=0;i<info->swapchainCount;++i) if(Contains(info->pSwapchains[i])) return true;
        return false;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL Images(VkDevice value,VkSwapchainKHR sc,uint32_t* count,VkImage* images)
    {
        const auto proc=Owns(value)&&Contains(sc) ? gdpa(value,"vkGetSwapchainImagesKHR") : VulkanHooks::NativeDeviceProc(value,"vkGetSwapchainImagesKHR");
        return ((PFN_vkGetSwapchainImagesKHR)proc)(value,sc,count,images);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL Acquire(VkDevice value,VkSwapchainKHR sc,uint64_t timeout,
        VkSemaphore semaphore,VkFence fence,uint32_t* index)
    {
        const auto proc=Owns(value)&&Contains(sc) ? gdpa(value,"vkAcquireNextImageKHR") : VulkanHooks::NativeDeviceProc(value,"vkAcquireNextImageKHR");
        return ((PFN_vkAcquireNextImageKHR)proc)(value,sc,timeout,semaphore,fence,index);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL Acquire2(VkDevice value,const VkAcquireNextImageInfoKHR* info,uint32_t* index)
    {
        // SL exposes only AcquireNextImageKHR. The equivalent single-device
        // operation is exact for mask 1; do not feed proxy handles to native 2KHR.
        if(Owns(value)&&Contains(info->swapchain))
        {
            if(info->deviceMask!=1) return VK_ERROR_FEATURE_NOT_PRESENT;
            return Acquire(value,info->swapchain,info->timeout,info->semaphore,info->fence,index);
        }
        return ((PFN_vkAcquireNextImage2KHR)VulkanHooks::NativeDeviceProc(value,"vkAcquireNextImage2KHR"))(value,info,index);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL WaitIdle(VkDevice value)
    {
        const auto proc=Owns(value) ? gdpa(value,"vkDeviceWaitIdle") : VulkanHooks::NativeDeviceProc(value,"vkDeviceWaitIdle");
        return ((PFN_vkDeviceWaitIdle)proc)(value);
    }
    static VKAPI_ATTR void VKAPI_CALL DestroySwapchain(VkDevice value,VkSwapchainKHR sc,const VkAllocationCallbacks* allocator)
    {
        if(Owns(value)&&Contains(sc)) {Frame::InvalidateFrame();MenuOverlayVk::ReleaseSwapchain(value,sc);}
        const auto proc=Owns(value)&&Contains(sc) ? gdpa(value,"vkDestroySwapchainKHR") : VulkanHooks::NativeDeviceProc(value,"vkDestroySwapchainKHR");
        ((PFN_vkDestroySwapchainKHR)proc)(value,sc,allocator);
        for(auto& entry:swapchains) if(entry==sc) entry=VK_NULL_HANDLE;
    }
    static VKAPI_ATTR void VKAPI_CALL DestroySurface(VkInstance value,VkSurfaceKHR surface,const VkAllocationCallbacks* allocator)
    {
        // After SL shutdown use the native destroy operation; its proxy still
        // refers to the plugin manager, which is no longer alive.
        const auto proc=Owns(value) ? gipa(value,"vkDestroySurfaceKHR") : VulkanHooks::NativeInstanceProc(value,"vkDestroySurfaceKHR");
        ((PFN_vkDestroySurfaceKHR)proc)(value,surface,allocator);
    }
    static bool BeforeDestroyDevice(VkDevice value)
    {
        if(!Owns(value)) return true;
        // Vulkan already requires applications to destroy their swapchains and
        // other device children before destroying the device.
        Frame::Stop();
        if(!StreamlineProxy::ShutdownVulkan()) return false;
        Frame::DestroyDevice(value);
        live=false; device=VK_NULL_HANDLE; swapchains={};
        return true;
    }
    static VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance value,const VkAllocationCallbacks* allocator)
    {
        if(Owns(value) && !StreamlineProxy::ShutdownVulkan()) return;
        ((PFN_vkDestroyInstance)VulkanHooks::NativeInstanceProc(value,"vkDestroyInstance"))(value,allocator);
        if(value==instance) { live=false; instance=VK_NULL_HANDLE; physicalCount=0; }
    }
    static PFN_vkVoidFunction Resolve(const char* name)
    {
        if(!Requested() || !name) return nullptr;
        if(strcmp(name,"vkGetSwapchainImagesKHR")==0) return (PFN_vkVoidFunction)Images;
        if(strcmp(name,"vkAcquireNextImageKHR")==0) return (PFN_vkVoidFunction)Acquire;
        if(strcmp(name,"vkAcquireNextImage2KHR")==0) return (PFN_vkVoidFunction)Acquire2;
        if(strcmp(name,"vkDeviceWaitIdle")==0) return (PFN_vkVoidFunction)WaitIdle;
        if(strcmp(name,"vkDestroySwapchainKHR")==0) return (PFN_vkVoidFunction)DestroySwapchain;
        if(strcmp(name,"vkDestroySurfaceKHR")==0) return (PFN_vkVoidFunction)DestroySurface;
        if(strcmp(name,"vkDestroyInstance")==0) return (PFN_vkVoidFunction)DestroyInstance;
        return nullptr;
    }
};
}
