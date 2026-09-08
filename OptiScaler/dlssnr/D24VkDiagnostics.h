#pragma once

#include <Util.h>
#include <Config.h>
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <share.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <array>
#include <vector>
#include <cstring>

namespace DlssNr::VkAudit
{
// The host has proved the native ABI, not the game's submission/layout contract.
// Keep this closed until completion tracking and game resource contracts are validated.
inline constexpr bool NativeExecutionValidated = false;

inline bool NativeArmed()
{
    static const bool armed = GetFileAttributesW((Util::DllPath().parent_path() / L"D24VulkanNR.enabled").c_str()) != INVALID_FILE_ATTRIBUTES;
    return armed;
}

inline bool Enabled()
{
    // Submission tracking is required even when diagnostic logging is off.
    return NativeArmed() || Config::Instance()->DlssNrDiagnostics.value_or_default()!=0;
}

inline void Write(const char* format, ...)
{
    if (Config::Instance()->DlssNrDiagnostics.value_or_default()==0)
        return;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    static FILE* file = [] {
        auto path = Util::DllPath().parent_path() / L"D24VulkanDiagnostics.log";
        return _wfsopen(path.c_str(), L"w", _SH_DENYNO);
    }();
    if (!file)
        return;
    va_list args;
    va_start(args, format);
    fprintf(file, "tick=%llu ", GetTickCount64());
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fflush(file);
}

struct RequestedFeatures
{
    bool bufferDeviceAddress = false;
    bool bufferDeviceAddressEXT = false;
    bool shaderInt64 = false;
    bool storageExtended = false;
    bool timelineSemaphore = false;
};

// Read known nodes only; never rewrite or truncate the application's pNext chain.
inline RequestedFeatures ReadFeatures(const VkDeviceCreateInfo& info)
{
    RequestedFeatures result;
    if (info.pEnabledFeatures)
    {
        result.shaderInt64 = info.pEnabledFeatures->shaderInt64 != VK_FALSE;
        result.storageExtended = info.pEnabledFeatures->shaderStorageImageExtendedFormats != VK_FALSE;
    }
    for (auto node = static_cast<const VkBaseInStructure*>(info.pNext); node; node = node->pNext)
    {
        switch (node->sType)
        {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
            result.shaderInt64 = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node)->features.shaderInt64 != VK_FALSE;
            result.storageExtended = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node)->features.shaderStorageImageExtendedFormats != VK_FALSE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
        {
            auto f = reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node);
            result.bufferDeviceAddress = f->bufferDeviceAddress != VK_FALSE;
            result.timelineSemaphore = f->timelineSemaphore != VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
            result.bufferDeviceAddress = reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(node)->bufferDeviceAddress != VK_FALSE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT:
            result.bufferDeviceAddressEXT = reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeaturesEXT*>(node)->bufferDeviceAddress != VK_FALSE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
            result.timelineSemaphore = reinterpret_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(node)->timelineSemaphore != VK_FALSE;
            break;
        default:
            break;
        }
    }
    return result;
}

struct StorageFeatureCopy
{
    struct alignas(8) Node { std::array<unsigned char,sizeof(VkPhysicalDeviceFeatures2)> data; };
    VkPhysicalDeviceFeatures base {};
    std::vector<Node> nodes;
    bool Enable(VkDeviceCreateInfo& info)
    {
        if(info.pEnabledFeatures)
        {
            base=*info.pEnabledFeatures;base.shaderStorageImageExtendedFormats=VK_TRUE;
            info.pEnabledFeatures=&base;return true;
        }
        nodes.clear();nodes.reserve(16);
        for(auto node=static_cast<const VkBaseInStructure*>(info.pNext);node;node=node->pNext)
        {
            size_t size=0;
            switch(node->sType)
            {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:size=sizeof(VkPhysicalDeviceFeatures2);break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES:size=sizeof(VkPhysicalDeviceSynchronization2Features);break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES:size=sizeof(VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures);break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:size=sizeof(VkPhysicalDeviceTimelineSemaphoreFeatures);break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT:size=sizeof(VkPhysicalDeviceBufferDeviceAddressFeaturesEXT);break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:size=sizeof(VkPhysicalDeviceBufferDeviceAddressFeatures);break;
            default:return false;
            }
            if(nodes.size()==16 || size>sizeof(Node))return false;
            nodes.emplace_back();std::memcpy(nodes.back().data.data(),node,size);
            if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
            {
                auto features=reinterpret_cast<VkPhysicalDeviceFeatures2*>(nodes.back().data.data());
                features->features.shaderStorageImageExtendedFormats=VK_TRUE;
                for(size_t i=0;i+1<nodes.size();++i)
                    reinterpret_cast<VkBaseOutStructure*>(nodes[i].data.data())->pNext=reinterpret_cast<VkBaseOutStructure*>(nodes[i+1].data.data());
                info.pNext=nodes.front().data.data();return true;
            }
        }
        base.shaderStorageImageExtendedFormats=VK_TRUE;info.pEnabledFeatures=&base;return true;
    }
};

inline void DeviceRequest(const char* phase, const VkDeviceCreateInfo& info)
{
    if (!Enabled())
        return;
    const auto f = ReadFeatures(info);
    Write("event=device_request phase=%s bda=%d bda_ext=%d int64=%d timeline=%d extensions=%u queues=%u",
          phase, f.bufferDeviceAddress, f.bufferDeviceAddressEXT, f.shaderInt64, f.timelineSemaphore,
          info.enabledExtensionCount, info.queueCreateInfoCount);
    for (uint32_t i = 0; i < info.enabledExtensionCount; ++i)
        Write("event=extension phase=%s name=%s", phase, info.ppEnabledExtensionNames[i]);
    for (uint32_t i = 0; i < info.queueCreateInfoCount; ++i)
        Write("event=queue_request phase=%s family=%u count=%u flags=%u", phase,
              info.pQueueCreateInfos[i].queueFamilyIndex, info.pQueueCreateInfos[i].queueCount,
              info.pQueueCreateInfos[i].flags);
    for (auto node = static_cast<const VkBaseInStructure*>(info.pNext); node; node = node->pNext)
        Write("event=feature_node phase=%s type=%d", phase, int(node->sType));
}

inline void Resource(const char* role, NVSDK_NGX_Resource_VK* resource)
{
    if (!resource || resource->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)
    {
        Write("event=resource role=%s present=%d image_view=0", role, resource != nullptr);
        return;
    }
    const auto& v = resource->Resource.ImageViewInfo;
    Write("event=resource role=%s present=1 image_view=1 format=%d width=%u height=%u aspect=%u mip=%u levels=%u layer=%u layers=%u rw=%d layout=unknown queue_owner=unknown",
          role, int(v.Format), v.Width, v.Height, v.SubresourceRange.aspectMask,
          v.SubresourceRange.baseMipLevel, v.SubresourceRange.levelCount,
          v.SubresourceRange.baseArrayLayer, v.SubresourceRange.layerCount, resource->ReadWrite);
}
}
