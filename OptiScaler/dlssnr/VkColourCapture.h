#include <dlssnr/BuildProfile.h>
#pragma once
#include "D24VkTracking.h"
#include "CaptureContract.h"
#include "CaptureControl.h"
#include "GuideCaptureVk.h"
#include "CaptureProfileVk.h"
#include <atomic>
#include <future>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <array>

namespace DlssNr::ColourCapture {
inline std::atomic<bool> requested{false};
inline std::atomic<Profile> requestedProfile{Profile::Full3};
inline std::mutex requestMutex;
inline std::atomic<int> displaySpace{-1};
inline std::mutex statusMutex;
inline std::string status="Off: no pixel capture requested";
inline void Status(std::string s){std::lock_guard lock(statusMutex);status=std::move(s);}
inline std::string Status(){std::lock_guard lock(statusMutex);return status;}
inline void RequestProfile(Profile profile,bool full8Allowed=false){
    if(!BuildProfile::PixelCapture){Status("Pixel capture requires the diagnostic build");return;}
    if(profile==Profile::Full8&&!full8Allowed){Status("Full-frame eight capture requires the process profile and diagnostics");return;}
    std::lock_guard lock(requestMutex);auto& job=capture::control::For(capture::control::Api::Vulkan);
    if(job.Peek().active)return; // Do not change the profile of an active request.
    requestedProfile.store(profile);
    if(!job.Request(FrameTarget(profile),GetTickCount64()))return;
    requested=true;Status("Armed: enable NR, close menu and wait 3 seconds");}
inline void Request(bool burst=false){RequestProfile(burst?Profile::Center8:Profile::Full3);}
inline void RequestFull8(bool allowed){RequestProfile(Profile::Full8,allowed);}

// Buffers remain owned by VkState until every recording lease has retired.
struct Batch {
    struct Slot {VkEvent event{};VkAudit::Lease lease{};bool pending=false,used=false,sealed=false;
        std::string metadata;std::filesystem::path prefix;std::future<bool> writer;std::array<std::unique_ptr<GuideReadback>,3> guides;};
    VkDevice device{};VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};
    VkDeviceSize bytes{},frameBytes{};uint32_t width{},height{},x{},y{};
    unsigned remaining=3,completed=0,delay=0,slots=1,next=0,current=0,target=3;Profile profile=Profile::Full3;bool burst=false,failed=false,timeoutReported=false;
    uint64_t started=0,guideBytes=0;bool cancelled=false;VkPhysicalDevice physical{};std::array<Slot,8> frames;
    void Cancel(){cancelled=true;remaining=0;Status("Cancelled: waiting for GPU retirement; completed files retained");}
    ~Batch(){for(auto& s:frames){if(s.writer.valid())s.writer.wait();if(s.event)vkDestroyEvent(device,s.event,nullptr);}
        if(mapped)vkUnmapMemory(device,memory);if(buffer)vkDestroyBuffer(device,buffer,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
    bool Init(VkDevice d,VkPhysicalDevice physical,uint32_t w,uint32_t h,Profile selected=Profile::Full3){
        device=d;this->physical=physical;profile=selected;const auto layout=CaptureLayout(profile,w,h);
        if(!layout.valid){Status(std::string("Capture refused: ")+layout.reason);return false;}
        burst=layout.burst;slots=layout.slots;target=remaining=layout.target;started=GetTickCount64();
        width=layout.width;height=layout.height;x=layout.x;y=layout.y;frameBytes=layout.frameBytes;bytes=layout.bytes;
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if(vkCreateBuffer(d,&b,nullptr,&buffer)!=VK_SUCCESS)return false;
        VkMemoryRequirements req{};vkGetBufferMemoryRequirements(d,buffer,&req);
        VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(physical,&props);uint32_t type=UINT32_MAX;
        for(uint32_t i=0;i<props.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&
            (props.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
        if(type==UINT32_MAX)return false;
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=type;
        if(vkAllocateMemory(d,&a,nullptr,&memory)!=VK_SUCCESS||vkBindBufferMemory(d,buffer,memory,0)!=VK_SUCCESS||
           vkMapMemory(d,memory,0,bytes,0,&mapped)!=VK_SUCCESS)return false;
        VkEventCreateInfo e{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
        for(unsigned i=0;i<slots;++i)if(vkCreateEvent(d,&e,nullptr,&frames[i].event)!=VK_SUCCESS)return false;
        VkAudit::Write("event=capture_begin api=Vulkan run=%llu frames=%u width=%u height=%u bytes=%llu burst=%u profile_schema=1 profile=%s scope=%s slots=%u",
            CaptureContract::RunId(),remaining,width,height,bytes,unsigned(burst),ProfileName(profile),ProfileScope(profile),slots);return true;
    }
    bool Poll(){
        bool busy=false;
        for(unsigned i=0;i<slots;++i){auto& s=frames[i];
            if(s.writer.valid()){
                if(s.writer.wait_for(std::chrono::seconds(0))!=std::future_status::ready){busy=true;continue;}
                const bool ok=s.writer.get();
                VkAudit::Write("event=capture_write api=Vulkan run=%llu epoch=%llu ok=%u bytes=%llu",CaptureContract::RunId(),s.lease.epoch,unsigned(ok),frameBytes);
                if(!ok){failed=true;remaining=0;Status("Capture file write failed; existing files retained");}
                else {++completed;if(remaining)--remaining;if(!failed)Status(remaining?"Saved "+std::to_string(completed)+"; waiting for remaining frames":"Complete: "+std::to_string(completed)+" colour packets saved");}
                capture::control::For(capture::control::Api::Vulkan).Progress(capture::control::Phase::Recording,completed);
                if(!burst){s.used=false;delay=30;}
            }
            if(s.pending){busy=true;if(!VkAudit::Ready(s.lease))continue;s.pending=false;
                if(cancelled)continue; // Retired buffers may be discarded; never map unretired work.
                if(!s.sealed||vkGetEventStatus(device,s.event)!=VK_EVENT_SET){failed=true;remaining=0;Status("Capture discarded: execution marker not observed");
                    VkAudit::Write("event=capture_discarded api=Vulkan run=%llu epoch=%llu",CaptureContract::RunId(),s.lease.epoch);continue;}
                CaptureContract::Append(s.metadata,"\"capture_completion\":{\"gpu_complete\":true,\"proof\":\"retired_lease_and_vk_event_set\",\"submission_epoch\":"+std::to_string(s.lease.epoch)+"}");
                std::string guideJson="\"guide_schema\":\"d18-guide-roi-v1\",\"guides\":{";const char* roles[]={"depth_model","motion_model","exposure_source"};
                std::array<GuideReadback*,3> guidePointers{};
                for(unsigned j=0;j<3;++j){if(j)guideJson+=",";capture::GuideEvidence e;e.role=roles[j];
                    if(s.guides[j]){e=s.guides[j]->evidence;guidePointers[j]=s.guides[j].get();}guideJson+="\""+std::string(roles[j])+"\":"+e.Json(true);
                    if(j<2){const auto raw=j==0?"depth_raw":"motion_raw";e.aliasOf=roles[j];e.role=raw;guideJson+=",\""+std::string(raw)+"\":"+e.Json(true);}}
                guideJson+="}";CaptureContract::Append(s.metadata,guideJson);
                auto path=s.prefix;auto meta=s.metadata;auto ptr=static_cast<const char*>(mapped)+i*frameBytes;auto size=frameBytes;
                capture::control::For(capture::control::Api::Vulkan).Progress(capture::control::Phase::Writing,completed);
                s.writer=std::async(std::launch::async,[path,meta,ptr,size,guidePointers]{try{
                    std::filesystem::create_directories(path.parent_path());
                    if(std::filesystem::exists(path.string()+".bin")||std::filesystem::exists(path.string()+".json"))return false;
                    std::ofstream raw(path.string()+".bin",std::ios::binary);raw.write(ptr,(std::streamsize)size);raw.close();if(!raw)return false;
                    for(auto* g:guidePointers)if(g&&!g->Write(path.parent_path()))return false;
                    std::ofstream json(path.string()+".json");json<<meta;json.close();return bool(json);
                }catch(...){return false;}});
            }
        }
        if(remaining&&GetTickCount64()-started>30000&&!timeoutReported){timeoutReported=true;failed=true;remaining=0;
            Status("Capture timed out; submitted resources retained until GPU retirement");
            VkAudit::Write("event=capture_timeout api=Vulkan run=%llu completed=%u",CaptureContract::RunId(),completed);}
        return !busy;
    }
    bool Begin(VkAudit::Lease l,std::string meta,std::filesystem::path path){
        if(!remaining||failed)return false;if(delay){--delay;return false;}
        current=burst?next:0;if(current>=slots)return false;auto& s=frames[current];
        if(s.used||s.pending||s.writer.valid())return false;
        for(auto& g:s.guides)if(g){if(g->evidence.recorded)guideBytes-=g->bytes;g.reset();}
        if(vkResetEvent(device,s.event)!=VK_SUCCESS){failed=true;remaining=0;Status("Capture event reset failed");return false;}
        s.lease=l;s.metadata=std::move(meta);s.prefix=std::move(path);s.used=s.pending=true;s.sealed=false;if(burst)++next;return true;
    }
    void Constants(const DlssNrConstants& encode,const DlssNrConstants& resolve){CaptureContract::Constants(frames[current].metadata,encode,resolve);}
    void Guides(VkCommandBuffer cmd,const NVSDK_NGX_Resource_VK* depth,const NVSDK_NGX_Resource_VK* motion,const NVSDK_NGX_Resource_VK* exposure,uint32_t outputW,uint32_t outputH,float sx,float sy){
        auto& s=frames[current];const NVSDK_NGX_Resource_VK* resources[]={depth,motion,exposure};const char* roles[]={"depth_model","motion_model","exposure_source"};
        for(unsigned j=0;j<3;++j){s.guides[j]=std::make_unique<GuideReadback>();s.guides[j]->Record(device,physical,cmd,resources[j],roles[j],outputW,outputH,j==1?sx:1,j==1?sy:1,s.prefix,guideBytes);}
        CaptureContract::Append(s.metadata,"\"guide_observation\":"+GuideResourcesVk::CoverageJson());
    }
    void Copy(VkCommandBuffer cmd,VkImage image,unsigned stage){
        const VkDeviceSize pixels=VkDeviceSize(width)*height,offsets[]={0,pixels*16,pixels*24,pixels*32};
        VkBufferImageCopy c{};c.bufferOffset=current*frameBytes+offsets[stage];c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        c.imageOffset={int32_t(x),int32_t(y),0};c.imageExtent={width,height,1};vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&c);
    }
    void Seal(VkCommandBuffer cmd){auto& s=frames[current];
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);
        vkCmdSetEvent(cmd,s.event,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);s.sealed=true;Status("Recorded: waiting for GPU completion");
        capture::control::For(capture::control::Api::Vulkan).Progress(capture::control::Phase::WaitingGpu,completed);
    }
};
}
