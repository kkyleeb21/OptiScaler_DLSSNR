#pragma once
#include "D24VkTracking.h"
#include <atomic>
#include <future>
#include <fstream>
#include <filesystem>
#include <mutex>

namespace DlssNr::ColourCapture {
inline std::atomic<bool> requested{false};
inline std::atomic<int> displaySpace{-1}; // latest observed swapchain; not inferred from DLSS flags
inline std::mutex statusMutex;
inline std::string status="Off: no pixel capture requested";
inline void Status(std::string s){std::lock_guard lock(statusMutex);status=std::move(s);}
inline std::string Status(){std::lock_guard lock(statusMutex);return status;}
inline void Request(){requested=true;Status("Armed: keep NR on and close menu; capture starts after 120 NR frames");}

// Owned by VkState, destroyed only after its command-buffer leases retire.
struct Batch {
    VkDevice device{}; VkBuffer buffer{}; VkDeviceMemory memory{}; VkEvent event{};
    void* mapped{}; VkDeviceSize bytes{}; uint32_t width{},height{};
    VkAudit::Lease lease{}; bool pending=false; unsigned remaining=3, completed=0,delay=120;
    std::string metadata; std::filesystem::path prefix;
    std::future<bool> writer;
    ~Batch(){ if(writer.valid())writer.wait(); if(mapped)vkUnmapMemory(device,memory);
        if(event)vkDestroyEvent(device,event,nullptr);if(buffer)vkDestroyBuffer(device,buffer,nullptr);
        if(memory)vkFreeMemory(device,memory,nullptr); }
    bool Init(VkDevice d,VkPhysicalDevice physical,uint32_t w,uint32_t h){
        device=d;width=w;height=h;bytes=VkDeviceSize(w)*h*48;
        if(!w||!h||bytes>512ull*1024*1024){Status("Capture refused: full-frame batch buffer exceeds 512 MiB");return false;}
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if(vkCreateBuffer(d,&b,nullptr,&buffer)!=VK_SUCCESS)return false;
        VkMemoryRequirements req{};vkGetBufferMemoryRequirements(d,buffer,&req);
        VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(physical,&props);
        uint32_t type=UINT32_MAX;
        for(uint32_t i=0;i<props.memoryTypeCount;++i)
            if((req.memoryTypeBits&(1u<<i)) && (props.memoryTypes[i].propertyFlags &
               (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
               (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
        if(type==UINT32_MAX)return false;
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=type;
        if(vkAllocateMemory(d,&a,nullptr,&memory)!=VK_SUCCESS || vkBindBufferMemory(d,buffer,memory,0)!=VK_SUCCESS ||
           vkMapMemory(d,memory,0,bytes,0,&mapped)!=VK_SUCCESS)return false;
        VkEventCreateInfo e{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
        return vkCreateEvent(d,&e,nullptr,&event)==VK_SUCCESS;
    }
    bool Poll(){
        if(writer.valid()){
            if(writer.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
            if(!writer.get()){remaining=0;Status("Capture file write failed");return true;}
            ++completed;--remaining;delay=30;
            Status(remaining?"Captured "+std::to_string(completed)+"/3; keep NR on":"Complete: 3 full-frame captures saved in D18ColourCaptures");
        }
        if(pending && VkAudit::Ready(lease)){
            pending=false;
            // Ready alone also covers discarded command buffers. The GPU event proves this batch ran.
            if(vkGetEventStatus(device,event)!=VK_EVENT_SET){remaining=0;Status("Capture discarded: no GPU execution marker observed");return true;}
            auto path=prefix;auto meta=metadata;auto ptr=mapped;auto size=bytes;
            writer=std::async(std::launch::async,[path,meta,ptr,size]{try{
                std::filesystem::create_directories(path.parent_path());
                std::ofstream raw(path.string()+".bin",std::ios::binary);raw.write((const char*)ptr,(std::streamsize)size);raw.close();
                if(!raw)return false;
                std::ofstream json(path.string()+".json");json<<meta;json.close();return bool(json);
            }catch(...){return false;}});
            Status("GPU completed; saving capture asynchronously");
        }
        return !pending&&!writer.valid();
    }
    bool Begin(VkAudit::Lease l,std::string meta,std::filesystem::path path){
        if(!remaining||pending||writer.valid())return false;
        if(delay){--delay;return false;}
        if(vkResetEvent(device,event)!=VK_SUCCESS){remaining=0;Status("Capture event reset failed");return false;}
        lease=l;metadata=std::move(meta);prefix=std::move(path);pending=true;return true;
    }
    void Copy(VkCommandBuffer cmd,VkImage image,unsigned stage){
        const VkDeviceSize pixels=VkDeviceSize(width)*height;
        const VkDeviceSize offsets[]={0,pixels*16,pixels*24,pixels*32};
        VkBufferImageCopy c{};c.bufferOffset=offsets[stage];c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        c.imageExtent={width,height,1};vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&c);
    }
    void Seal(VkCommandBuffer cmd){
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        vkCmdSetEvent(cmd,event,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        Status("Recorded: waiting for observed GPU completion");
    }
};
}
