#pragma once
#include <framegen/VulkanFgSwapchain.h>
#include "VulkanFgResourceState.h"
namespace VulkanFg::Resources
{
inline VkResult VKAPI_CALL CreateImage(VkDevice device,const VkImageCreateInfo* info,const VkAllocationCallbacks* allocator,VkImage* result)
{
    const auto status=((PFN_vkCreateImage)VulkanHooks::NativeDeviceProc(device,"vkCreateImage"))(device,info,allocator,result);
    if(status==VK_SUCCESS && SwapchainRoute::Owns(device))
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(images.size()<8192)images[*result]={info->usage,info->flags,info->extent,info->format,info->mipLevels,info->arrayLayers,++generation};
    }
    return status;
}
inline void VKAPI_CALL DestroyImage(VkDevice device,VkImage image,const VkAllocationCallbacks* allocator)
{
    if(SwapchainRoute::Owns(device))
    { Frame::DestroyImage(device,image);std::lock_guard<std::mutex> lock(mutex);images.erase(image); }
    ((PFN_vkDestroyImage)VulkanHooks::NativeDeviceProc(device,"vkDestroyImage"))(device,image,allocator);
}
inline VkResult VKAPI_CALL CreateView(VkDevice device,const VkImageViewCreateInfo* info,const VkAllocationCallbacks* allocator,VkImageView* result)
{
    const auto status=((PFN_vkCreateImageView)VulkanHooks::NativeDeviceProc(device,"vkCreateImageView"))(device,info,allocator,result);
    if(status==VK_SUCCESS && SwapchainRoute::Owns(device))
    {
        std::lock_guard<std::mutex> lock(mutex);const auto image=images.find(info->image);
        if(image!=images.end() && views.size()<16384)
            views[*result]={info->image,info->format,info->viewType,info->subresourceRange,image->second.generation,++generation};
    }
    return status;
}
inline void VKAPI_CALL DestroyView(VkDevice device,VkImageView view,const VkAllocationCallbacks* allocator)
{
    if(SwapchainRoute::Owns(device))
    { std::lock_guard<std::mutex> lock(mutex);views.erase(view); }
    ((PFN_vkDestroyImageView)VulkanHooks::NativeDeviceProc(device,"vkDestroyImageView"))(device,view,allocator);
}
inline void VKAPI_CALL WriteDescriptors(VkDevice device,uint32_t count,const VkWriteDescriptorSet* writes,
    uint32_t copyCount,const VkCopyDescriptorSet* copies)
{
    ((PFN_vkUpdateDescriptorSets)VulkanHooks::NativeDeviceProc(device,"vkUpdateDescriptorSets"))(device,count,writes,copyCount,copies);
    ObserveWrites(count,writes);
}
inline PFN_vkCmdPushDescriptorSetKHR originalPush{};
inline void VKAPI_CALL PushDescriptors(VkCommandBuffer command,VkPipelineBindPoint point,VkPipelineLayout layout,
    uint32_t set,uint32_t count,const VkWriteDescriptorSet* writes)
{
    originalPush(command,point,layout,set,count,writes);
    if(active && active->command==command)ObserveWrites(count,writes);
}
inline PFN_vkVoidFunction Resolve(const char* name,PFN_vkVoidFunction original)
{
    if(!original || !SwapchainRoute::Requested())return nullptr;
    if(strcmp(name,"vkCreateImage")==0)return (PFN_vkVoidFunction)CreateImage;
    if(strcmp(name,"vkDestroyImage")==0)return (PFN_vkVoidFunction)DestroyImage;
    if(strcmp(name,"vkCreateImageView")==0)return (PFN_vkVoidFunction)CreateView;
    if(strcmp(name,"vkDestroyImageView")==0)return (PFN_vkVoidFunction)DestroyView;
    if(strcmp(name,"vkUpdateDescriptorSets")==0)return (PFN_vkVoidFunction)WriteDescriptors;
    if(strcmp(name,"vkCmdPushDescriptorSetKHR")==0)
    {if(!originalPush)originalPush=(PFN_vkCmdPushDescriptorSetKHR)original;return (PFN_vkVoidFunction)PushDescriptors;}
    return nullptr;
}
}
