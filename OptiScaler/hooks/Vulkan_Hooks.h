#pragma once
#include "SysUtils.h"

class VulkanHooks
{
  public:
    static PFN_vkCreateSemaphore o_vkCreateSemaphore;
    static PFN_vkSignalSemaphore o_vkSignalSemaphore;
    static PFN_vkAntiLagUpdateAMD o_vkAntiLagUpdateAMD;

    static void Hook(HMODULE vulkan1);
    static void Unhook();

    // Register only D18's own Vulkan interposer before slInit. The caller owns
    // the module lease and must stop SL before clearing this registration.
    static bool RegisterOwnedFgRuntime(HMODULE module);
    static void ReleaseOwnedFgRuntime(HMODULE module);
    static FARPROC ResolveOwnedFgExport(HMODULE module, const char* name, void* caller);
    static FARPROC ResolveFgGameExport(HMODULE module, const char* name);
    static PFN_vkVoidFunction NativeInstanceProc(VkInstance instance, const char* name);
    static PFN_vkVoidFunction NativeDeviceProc(VkDevice device, const char* name);
    class RuntimeSetupScope
    {
    public:
        RuntimeSetupScope();
        ~RuntimeSetupScope();
        RuntimeSetupScope(const RuntimeSetupScope&)=delete;
        RuntimeSetupScope& operator=(const RuntimeSetupScope&)=delete;
    };
};
