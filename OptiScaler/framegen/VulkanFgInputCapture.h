#pragma once
#include "VulkanFgResourceState.h"
#include <dlssnr/D24VkDiagnostics.h>
#include <atomic>

namespace VulkanFg::InputCapture
{
// CPU-only observations. Neither descriptor declarations nor explicit barriers
// prove absence of render-pass/secondary-buffer writes or aliasing at Present.
// This probe cannot enable FG and allocates no GPU resources.
struct Pending
{
    Resources::Observation input{};
    VkQueue queue{};
    unsigned handoffs=0,submissions=0,laterBarriers=0,otherCommands=0;
    bool invalidated=false,failed=false;
};
inline std::mutex mutex;
inline Pending pending{};
inline std::atomic<bool> watching=false;
inline std::atomic<unsigned> records=0;
inline bool Armed()
{
    return Config::Instance()->DlssNrDiagnostics.value_or_default()!=0 && records.load(std::memory_order_relaxed)<32;
}
inline void Capture(const Resources::Observation& value)
{
    if(!Armed())return;
    std::lock_guard<std::mutex> lock(mutex);
    const auto handoffs=pending.handoffs+1;
    pending={};pending.input=value;pending.handoffs=handoffs;
    watching.store(true,std::memory_order_release);
}
template<class Barrier> inline void Barriers(VkCommandBuffer command,uint32_t count,const Barrier* barriers)
{
    if(!watching.load(std::memory_order_acquire)||!barriers)return;
    std::lock_guard<std::mutex> lock(mutex);
    for(uint32_t n=0;n<count;++n)for(auto& guide:pending.input.guides)
    {
        const auto& b=barriers[n];
        if(!guide.image||guide.image!=b.image)continue;
        if(command!=pending.input.command){++pending.otherCommands;guide.known=false;continue;}
        ++pending.laterBarriers;
        if(b.srcQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED && b.dstQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED &&
           b.srcQueueFamilyIndex!=b.dstQueueFamilyIndex)guide.ownershipConflict=true;
        if(guide.view && Resources::ContainsRange(b.subresourceRange,guide.sampled.range,guide.description))
        {guide.layout=b.newLayout;guide.known=Resources::SampledLayout(b.newLayout);}
    }
}
inline void Invalidate(VkCommandBuffer command)
{
    if(!watching.load(std::memory_order_acquire))return;
    std::lock_guard<std::mutex> lock(mutex);
    if(command==pending.input.command)pending.invalidated=true;
}
inline VkCommandBuffer Command(const VkSubmitInfo& info,uint32_t index){return info.pCommandBuffers[index];}
inline VkCommandBuffer Command(const VkSubmitInfo2& info,uint32_t index){return info.pCommandBufferInfos[index].commandBuffer;}
inline uint32_t Count(const VkSubmitInfo& info){return info.commandBufferCount;}
inline uint32_t Count(const VkSubmitInfo2& info){return info.commandBufferInfoCount;}
template<class Submit> inline void Submitted(VkQueue queue,uint32_t count,const Submit* infos,VkResult result)
{
    if(!watching.load(std::memory_order_acquire)||!infos)return;
    std::lock_guard<std::mutex> lock(mutex);
    for(uint32_t i=0;i<count;++i)for(uint32_t j=0;j<Count(infos[i]);++j)
        if(Command(infos[i],j)==pending.input.command)
        {++pending.submissions;pending.queue=queue;pending.failed|=result!=VK_SUCCESS;}
}
inline void Present(VkQueue queue,VkResult result)
{
    if(!watching.load(std::memory_order_acquire))return;
    Pending sample;
    {
        std::lock_guard<std::mutex> lock(mutex);
        sample=pending;pending={};watching.store(false,std::memory_order_release);
    }
    if(!Armed())return;
    const auto record=records.fetch_add(1,std::memory_order_relaxed)+1;
    DlssNr::VkAudit::Write("event=fg_sampled_input record=%u feature=%llu cmd=%p handoffs=%u submitted=%u same_queue=%d invalidated=%d failed=%d descriptor_writes=%u sr_barriers=%u later_barriers=%u other_commands=%u result=%d coverage=partial reuse_safe=unknown",
        record,(unsigned long long)sample.input.feature,(void*)sample.input.command,sample.handoffs,sample.submissions,
        sample.submissions==1&&sample.queue==queue,sample.invalidated,sample.failed,sample.input.descriptorWrites,
        sample.input.barriers,sample.laterBarriers,sample.otherCommands,int(result));
    for(size_t n=0;n<sample.input.guides.size();++n)
    {
        const auto& guide=sample.input.guides[n];
        DlssNr::VkAudit::Write("event=fg_sampled_guide record=%u role=%s image=%p view=%p image_generation=%llu view_generation=%llu alive=%d metadata=%d sampled_layout=%d present_layout=%d layout_known=%d ownership_conflict=%d aspect=%u format=%u usage=%u width=%u height=%u mips=%u layers=%u view_type=%u coverage=descriptor_contract",
            record,n==0?"depth":"motion",(void*)guide.image,(void*)guide.view,(unsigned long long)guide.description.generation,
            (unsigned long long)guide.sampled.viewGeneration,Resources::StillAlive(guide),guide.description.generation!=0,
            int(guide.descriptorLayout),int(guide.layout),guide.known,guide.ownershipConflict,guide.sampled.range.aspectMask,
            unsigned(guide.sampled.format),guide.description.usage,guide.description.extent.width,guide.description.extent.height,
            guide.description.mips,guide.description.layers,unsigned(guide.sampled.type));
    }
}
}
