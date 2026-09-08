#pragma once
#include "D24VkDiagnostics.h"
#include <unordered_map>
#include <vector>

namespace DlssNr::VkAudit
{
struct BarrierObservation
{
    VkImage image;
    VkImageSubresourceRange range;
    VkImageLayout layout;
    uint32_t sourceFamily, destinationFamily;
};
struct Recording
{
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    uint32_t family = UINT32_MAX;
    uint64_t epoch = 0;
    std::vector<BarrierObservation> barriers;
    VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
};
struct CompletionSample
{
    VkCommandBuffer cmd;
    VkDevice device;
    uint64_t epoch;
    bool invalidated = false;
    bool submitted = false;
    bool failed = false;
    bool reported = false;
    std::vector<VkFence> pending;
    unsigned submitting = 0;
    bool live = false;
};
struct DeviceFunctions
{
    PFN_vkCreateFence create;
    PFN_vkGetFenceStatus status;
    PFN_vkDestroyFence destroy;
    PFN_vkDeviceWaitIdle idle;
    bool storageExtended = false;
    std::vector<VkQueueFamilyProperties> families;
};
inline std::mutex trackingMutex;
inline std::unordered_map<VkCommandBuffer, Recording> recordings;
inline std::unordered_map<VkDevice, DeviceFunctions> deviceFunctions;
inline std::vector<CompletionSample> completionSamples;
inline uint64_t nextEpoch = 0;
inline unsigned fenceBudget = 64;

inline void RegisterDevice(VkDevice device, PFN_vkGetDeviceProcAddr get, bool storageExtended = true)
{
    if (!Enabled() || !get) return;
    std::lock_guard<std::mutex> lock(trackingMutex);
    deviceFunctions[device] = {
        (PFN_vkCreateFence)get(device,"vkCreateFence"), (PFN_vkGetFenceStatus)get(device,"vkGetFenceStatus"),
        (PFN_vkDestroyFence)get(device,"vkDestroyFence"), (PFN_vkDeviceWaitIdle)get(device,"vkDeviceWaitIdle"), storageExtended };
    Write("event=storage_formats extended=%d",storageExtended);
}
inline bool ExtendedFormats(VkDevice device)
{
    std::lock_guard<std::mutex> lock(trackingMutex);
    auto found=deviceFunctions.find(device);
    return found!=deviceFunctions.end() && found->second.storageExtended;
}
inline void RegisterQueueFamilies(VkDevice device, std::vector<VkQueueFamilyProperties> families)
{
    std::lock_guard<std::mutex> lock(trackingMutex);
    auto found=deviceFunctions.find(device);
    if(found!=deviceFunctions.end()) found->second.families=std::move(families);
}
// Caller holds trackingMutex. Completion alone does not invalidate executable command buffers.
inline void PollLocked()
{
    for (auto& sample : completionSamples)
    {
        auto f = deviceFunctions.find(sample.device);
        if (f == deviceFunctions.end()) continue;
        for (auto it = sample.pending.begin(); it != sample.pending.end();)
        {
            const auto result = f->second.status(sample.device, *it);
            if (result == VK_SUCCESS)
            {
                f->second.destroy(sample.device, *it, nullptr);
                it = sample.pending.erase(it);
                if (!sample.live) Write("event=gpu_completed epoch=%llu", (unsigned long long)sample.epoch);
            }
            else
            {
                if (result != VK_NOT_READY) sample.failed = true;
                ++it;
            }
        }
        if (sample.invalidated && !sample.submitting && sample.pending.empty() && !sample.failed && !sample.reported)
        {
            sample.reported = true;
            if (!sample.live) Write("event=retirement_ready epoch=%llu submitted=%d", (unsigned long long)sample.epoch, sample.submitted);
        }
    }
}
inline void Allocate(VkDevice device, VkCommandPool pool, uint32_t family, uint32_t count, const VkCommandBuffer* cmds,
                     VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY)
{
    if (!Enabled()) return;
    std::lock_guard<std::mutex> lock(trackingMutex);
    for (uint32_t i=0; i<count && recordings.size()<1024; ++i)
        recordings[cmds[i]] = {device,pool,family,0,{},level};
}
inline void InvalidateLocked(VkCommandBuffer cmd)
{
    auto it = recordings.find(cmd);
    if (it == recordings.end()) return;
    for (auto& sample : completionSamples)
        if (sample.cmd == cmd && sample.epoch == it->second.epoch) sample.invalidated = true;
    it->second.epoch = ++nextEpoch;
    it->second.barriers.clear();
}
inline void Invalidate(VkCommandBuffer cmd)
{
    if (!Enabled()) return;
    std::lock_guard<std::mutex> lock(trackingMutex);
    InvalidateLocked(cmd);
    PollLocked();
}
inline void InvalidatePool(VkCommandPool pool, bool erase)
{
    if (!Enabled()) return;
    std::lock_guard<std::mutex> lock(trackingMutex);
    for (auto it=recordings.begin(); it!=recordings.end();)
    {
        if (it->second.pool != pool) {++it; continue;}
        InvalidateLocked(it->first);
        if (erase) it=recordings.erase(it); else ++it;
    }
    PollLocked();
}
inline void Free(uint32_t count, const VkCommandBuffer* cmds)
{
    if (!Enabled()) return;
    std::lock_guard<std::mutex> lock(trackingMutex);
    for(uint32_t i=0;i<count;++i){InvalidateLocked(cmds[i]); recordings.erase(cmds[i]);}
    PollLocked();
}
template<class Barrier> inline void ObserveBarriers(VkCommandBuffer cmd, uint32_t count, const Barrier* barriers)
{
    if (!Enabled() || !count) return;
    std::lock_guard<std::mutex> lock(trackingMutex);
    auto it=recordings.find(cmd);
    if(it==recordings.end()) return;
    auto& out=it->second.barriers;
    for(uint32_t i=0;i<count;++i)
    {
        if(out.size()==512) out.erase(out.begin());
        const auto& b=barriers[i];
        out.push_back({b.image,b.subresourceRange,b.newLayout,b.srcQueueFamilyIndex,b.dstQueueFamilyIndex});
    }
}
inline void Handoff(VkCommandBuffer cmd, NVSDK_NGX_Resource_VK* const* resources, const char* const* roles, unsigned count)
{
    if (!Enabled()) return;
    std::lock_guard<std::mutex> lock(trackingMutex);
    PollLocked();
    auto it=recordings.find(cmd);
    if(it==recordings.end()){Write("event=recording tracked=0"); return;}
    const auto& record=it->second;
    if(record.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY || !record.epoch)
    {Write("event=recording tracked=0 reason=secondary_or_begin_not_observed");return;}
    Write("event=recording tracked=1 epoch=%llu family=%u",(unsigned long long)record.epoch,record.family);
    if(completionSamples.size()<32)
        completionSamples.push_back({cmd,record.device,record.epoch,false,false,false,false,{}});
    for(unsigned i=0;i<count;++i)
    {
        if(!resources[i] || resources[i]->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW) continue;
        const auto& v=resources[i]->Resource.ImageViewInfo;
        // Report the most recent observed barrier, including its exact range. This is evidence,
        // not a whole-image layout guarantee: render passes and secondary buffers are not tracked.
        bool found=false;
        for(auto b=record.barriers.rbegin();b!=record.barriers.rend();++b)
            if(b->image==v.Image)
            {
                Write("event=last_barrier role=%s layout=%d aspect=%u mip=%u levels=%u layer=%u layers=%u source_family=%u destination_family=%u coverage=partial",
                      roles[i],int(b->layout),b->range.aspectMask,b->range.baseMipLevel,b->range.levelCount,
                      b->range.baseArrayLayer,b->range.layerCount,b->sourceFamily,b->destinationFamily);
                found=true;break;
            }
        if(!found) Write("event=last_barrier role=%s layout=unknown coverage=none",roles[i]);
    }
}
struct Lease
{
    size_t index = SIZE_MAX;
    uint64_t epoch = 0;
    bool Valid() const { return index != SIZE_MAX; }
};
inline bool Ready(Lease lease)
{
    if (!lease.Valid()) return true;
    std::lock_guard<std::mutex> lock(trackingMutex);
    PollLocked();
    return lease.index < completionSamples.size() &&
        (completionSamples[lease.index].epoch != lease.epoch || completionSamples[lease.index].reported);
}
inline Lease Acquire(VkCommandBuffer cmd, VkDevice device, const char** reason = nullptr)
{
    auto reject=[&](const char* why)->Lease { if(reason) *reason=why; return {}; };
    if(reason) *reason=nullptr;
    if (!Enabled()) return reject("Vulkan tracking disabled");
    std::lock_guard<std::mutex> lock(trackingMutex);
    PollLocked();
    auto record=recordings.find(cmd);
    if(record==recordings.end()) return reject("Vulkan command buffer allocation not observed");
    if(record->second.device!=device) return reject("Vulkan command buffer device mismatch");
    if(!record->second.epoch) return reject("Vulkan command buffer recording not observed");
    if(record->second.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY) return reject("secondary Vulkan command buffer");
    auto functions=deviceFunctions.find(device);
    if(functions==deviceFunctions.end() || record->second.family>=functions->second.families.size())
        return reject("Vulkan queue family capabilities unavailable");
    const auto& family=functions->second.families[record->second.family];
    if(!family.queueCount || !(family.queueFlags & VK_QUEUE_COMPUTE_BIT))
        return reject("Vulkan queue family lacks compute support");
    for(size_t i=0;i<completionSamples.size();++i)
        if(completionSamples[i].live && completionSamples[i].cmd==cmd && completionSamples[i].epoch==record->second.epoch && !completionSamples[i].invalidated)
            return {i,record->second.epoch};
    size_t slot=completionSamples.size();
    for(size_t i=0;i<completionSamples.size();++i) if(completionSamples[i].live && completionSamples[i].reported){slot=i;break;}
    if(slot>=128) return reject("Vulkan completion tracking capacity reached");
    CompletionSample sample{cmd,device,record->second.epoch,false,false,false,false,{},0,true};
    if(slot==completionSamples.size()) completionSamples.push_back(sample); else completionSamples[slot]=sample;
    return {slot,record->second.epoch};
}
inline std::vector<size_t> Prepare(const std::vector<VkCommandBuffer>& cmds)
{
    std::vector<size_t> batch;
    if(!Enabled()) return batch;
    std::lock_guard<std::mutex> lock(trackingMutex);
    for(size_t i=0;i<completionSamples.size();++i)
    {
        auto& sample=completionSamples[i];
        for(auto cmd:cmds)
        {
            auto recording=recordings.find(cmd);
            if(recording!=recordings.end() && sample.cmd==cmd && sample.epoch==recording->second.epoch && !sample.invalidated)
            {++sample.submitting;batch.push_back(i);break;}
        }
    }
    return batch;
}
template<class SubmitEmpty> inline void Submitted(VkQueue queue, const std::vector<size_t>& batch, VkResult result, SubmitEmpty submitEmpty)
{
    if(!Enabled()) return;
    std::lock_guard<std::mutex> lock(trackingMutex);
    PollLocked();
    for(auto index:batch)
    {
            auto& sample=completionSamples[index];
            --sample.submitting;
            static unsigned liveSamples=0;
            if(sample.live) {
                const auto n=++liveSamples;
                if(n<=16 || (n<=24000 && n%120==0))
                    Write("event=nr_submit sample=%u cmd=%p epoch=%llu queue=%p result=%d",n,(void*)sample.cmd,
                        (unsigned long long)sample.epoch,(void*)queue,int(result));
            }
            if (!sample.live) Write("event=sr_submit epoch=%llu queue=%p result=%d",(unsigned long long)sample.epoch,(void*)queue,int(result));
            if(result!=VK_SUCCESS){sample.failed=true;continue;}
            auto f=deviceFunctions.find(sample.device);
            if((!sample.live && !fenceBudget) || f==deviceFunctions.end() || !f->second.create || !f->second.status || !f->second.destroy){sample.failed=true;continue;}
            if (!sample.live) --fenceBudget;
            VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence{};
            if(f->second.create(sample.device,&info,nullptr,&fence)!=VK_SUCCESS){sample.failed=true;continue;}
            // A private empty submission on the same queue fences earlier work. Never borrow/reset
            // the game's fence. The original queue function avoids recursive interception.
            const auto fenced=submitEmpty(fence);
            sample.submitted=true;
            sample.pending.push_back(fence);
            if(fenced!=VK_SUCCESS) sample.failed=true;
            if (!sample.live || fenced != VK_SUCCESS) Write("event=completion_fence epoch=%llu result=%d",(unsigned long long)sample.epoch,int(fenced));
    }
}
inline std::vector<VkCommandBuffer> Commands(uint32_t count,const VkSubmitInfo* submits)
{
    std::vector<VkCommandBuffer> out;
    if(Enabled()) for(uint32_t i=0;i<count;++i) for(uint32_t j=0;j<submits[i].commandBufferCount;++j) out.push_back(submits[i].pCommandBuffers[j]);
    return out;
}
inline std::vector<VkCommandBuffer> Commands(uint32_t count,const VkSubmitInfo2* submits)
{
    std::vector<VkCommandBuffer> out;
    if(Enabled()) for(uint32_t i=0;i<count;++i) for(uint32_t j=0;j<submits[i].commandBufferInfoCount;++j) out.push_back(submits[i].pCommandBufferInfos[j].commandBuffer);
    return out;
}
inline void DestroyDevice(VkDevice device)
{
    if(!Enabled()) return;
    std::lock_guard<std::mutex> lock(trackingMutex);
    auto f=deviceFunctions.find(device);
    if(f==deviceFunctions.end()) return;
    bool pending=false;
    for(const auto& sample:completionSamples) if(sample.device==device && !sample.pending.empty()) pending=true;
    const auto idle=pending && f->second.idle ? f->second.idle(device) : VK_SUCCESS;
    for(auto& sample:completionSamples) if(sample.device==device)
    {
        if(idle==VK_SUCCESS || idle==VK_ERROR_DEVICE_LOST)
            for(auto fence:sample.pending) f->second.destroy(device,fence,nullptr);
        sample.pending.clear(); sample.failed=true;
    }
    for(auto it=recordings.begin();it!=recordings.end();) if(it->second.device==device) it=recordings.erase(it); else ++it;
    deviceFunctions.erase(f);
    Write("event=device_tracking_destroy idle=%d",int(idle));
}
}
