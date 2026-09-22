#pragma once
#include "GuideResourcesVk.h"
#include <fstream>
#include <filesystem>

namespace DlssNr::ColourCapture {
struct GuideReadback {
    VkDevice device{};VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize bytes=0;
    capture::GuideEvidence evidence;
    ~GuideReadback(){if(mapped)vkUnmapMemory(device,memory);if(buffer)vkDestroyBuffer(device,buffer,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
    void Record(VkDevice d,VkPhysicalDevice physical,VkCommandBuffer cmd,const NVSDK_NGX_Resource_VK* resource,const char* role,
                uint32_t outputW,uint32_t outputH,float sx,float sy,const std::filesystem::path& prefix,uint64_t& budget){
        device=d;auto plan=GuideResourcesVk::Inspect(d,cmd,resource,role,outputW,outputH,sx,sy);evidence=plan.evidence;
        if(!plan.image)return;auto& e=evidence;bytes=VkDeviceSize(e.roi.width)*e.roi.height*e.texelBytes;
        if(!bytes||bytes>4ull*1024*1024||budget+bytes>96ull*1024*1024){e.reason="guide_readback_budget";bytes=0;return;}
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if(vkCreateBuffer(d,&b,nullptr,&buffer)!=VK_SUCCESS){e.reason="readback_buffer_failed";return;}
        VkMemoryRequirements req{};vkGetBufferMemoryRequirements(d,buffer,&req);VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(physical,&props);uint32_t type=UINT32_MAX;
        for(uint32_t i=0;i<props.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
        if(type==UINT32_MAX){e.reason="coherent_host_memory_unavailable";return;}
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=type;
        if(vkAllocateMemory(d,&a,nullptr,&memory)!=VK_SUCCESS||vkBindBufferMemory(d,buffer,memory,0)!=VK_SUCCESS||vkMapMemory(d,memory,0,bytes,0,&mapped)!=VK_SUCCESS){e.reason="readback_memory_failed";return;}
        budget+=bytes;VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=plan.image;barrier.subresourceRange=plan.transition;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout=plan.layout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource=plan.copy;copy.imageOffset={int32_t(e.roi.x),int32_t(e.roi.y),0};copy.imageExtent={e.roi.width,e.roi.height,1};
        vkCmdCopyImageToBuffer(cmd,plan.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        std::swap(barrier.oldLayout,barrier.newLayout);barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        e.file=prefix.filename().string()+"."+role+".raw";e.recorded=true;e.reason="";
    }
    bool Write(const std::filesystem::path& directory) const {
        if(!evidence.recorded)return true;const auto path=directory/evidence.file;if(std::filesystem::exists(path))return false;
        std::ofstream f(path,std::ios::binary);f.write(static_cast<const char*>(mapped),std::streamsize(bytes));f.close();return bool(f);
    }
};
}
