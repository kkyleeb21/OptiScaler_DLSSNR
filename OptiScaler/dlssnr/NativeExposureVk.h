#pragma once
#include "D24VkTracking.h"
#include <cmath>
#include <cstring>
namespace DlssNr {
// One owned 1x1 copy. Its containing VkState retains the recording lease through retirement.
struct NativeExposureVk {
    VkDevice device{}; VkBuffer buffer{}; VkDeviceMemory memory{}; VkEvent event{};
    void* mapped=nullptr; VkAudit::Lease lease{}; bool pending=false;
    float value=0,pre=1,pendingPre=1;
    ~NativeExposureVk(){if(!device)return;if(mapped)vkUnmapMemory(device,memory);
        if(event)vkDestroyEvent(device,event,nullptr);if(buffer)vkDestroyBuffer(device,buffer,nullptr);
        if(memory)vkFreeMemory(device,memory,nullptr);}
    bool Init(VkDevice d,VkPhysicalDevice p){
        device=d;VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=16;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if(vkCreateBuffer(d,&b,nullptr,&buffer)!=VK_SUCCESS)return false;
        VkMemoryRequirements req{};vkGetBufferMemoryRequirements(d,buffer,&req);
        VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(p,&props);
        uint32_t type=UINT32_MAX;for(uint32_t i=0;i<props.memoryTypeCount;++i)
            if((req.memoryTypeBits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
        if(type==UINT32_MAX)return false;
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=type;
        if(vkAllocateMemory(d,&a,nullptr,&memory)!=VK_SUCCESS||vkBindBufferMemory(d,buffer,memory,0)!=VK_SUCCESS||vkMapMemory(d,memory,0,16,0,&mapped)!=VK_SUCCESS)return false;
        VkEventCreateInfo e{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};return vkCreateEvent(d,&e,nullptr,&event)==VK_SUCCESS;
    }
    void Poll(){if(!pending||!VkAudit::Ready(lease))return;pending=false;
        if(vkGetEventStatus(device,event)!=VK_EVENT_SET)return;
        float x=0;std::memcpy(&x,mapped,sizeof(x));
        if(std::isfinite(x)&&x>1e-6f){value=x;pre=pendingPre;}}
    bool Begin(VkAudit::Lease l,float p){if(pending||!std::isfinite(p)||p<=0||vkResetEvent(device,event)!=VK_SUCCESS)return false;
        lease=l;pendingPre=p;pending=true;return true;}
    void Copy(VkCommandBuffer cmd,VkImage image){
        VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={1,1,1};
        vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&c);
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);
        vkCmdSetEvent(cmd,event,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }
};
inline bool ReadableExposure(VkCommandBuffer cmd,const NVSDK_NGX_Resource_VK* resource,VkImageLayout& layout){
    if(!resource||resource->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)return false;
    const auto& v=resource->Resource.ImageViewInfo;const auto& r=v.SubresourceRange;
    if(v.Width!=1||v.Height!=1||r.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||r.levelCount!=1||r.layerCount!=1||
       (v.Format!=VK_FORMAT_R32_SFLOAT&&v.Format!=VK_FORMAT_R16_SFLOAT))return false;
    std::lock_guard lock(VkAudit::trackingMutex);auto it=VkAudit::recordings.find(cmd);if(it==VkAudit::recordings.end())return false;
    for(size_t recent=0;recent<it->second.barriers.size();++recent){
        const auto* b=&it->second.barriers.newest(recent);
        if(b->image!=v.Image)continue;
        if(b->range.baseMipLevel!=r.baseMipLevel||b->range.baseArrayLayer!=r.baseArrayLayer||b->range.levelCount!=1||b->range.layerCount!=1)return false;
        if(b->destinationFamily!=VK_QUEUE_FAMILY_IGNORED&&b->destinationFamily!=it->second.family)return false;
        layout=b->layout;return layout==VK_IMAGE_LAYOUT_GENERAL||layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }return false;
}
}
