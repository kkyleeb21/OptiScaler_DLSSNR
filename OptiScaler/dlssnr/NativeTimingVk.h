#pragma once
#include "D24VkTracking.h"
#include <array>
#include <optional>
#include <cmath>

namespace DlssNr {
// The containing VkState retains every recording lease until destruction. No independent fence,
// queue submission, image copy or CPU wait is introduced by this meter.
struct NativeTimingVk {
    struct Slot { VkAudit::Lease lease{}; uint32_t bits=0; bool pending=false, sealed=false; };
    VkDevice device{};
    VkQueryPool pool{};
    std::array<VkEvent,4> executed{};
    std::array<Slot,4> slots{};
    double period=0;
    std::optional<double> lastMs;
    uint64_t lastRead=0, nextSample=0, nextLog=0;
    bool attempted=false, unavailable=false;
    NativeTimingVk()=default;
    NativeTimingVk(const NativeTimingVk&)=delete;
    NativeTimingVk& operator=(const NativeTimingVk&)=delete;
    ~NativeTimingVk(){for(auto event:executed)if(event)vkDestroyEvent(device,event,nullptr);if(pool)vkDestroyQueryPool(device,pool,nullptr);}

    // Ready includes command invalidation, not just GPU completion. A CPU-reset/GPU-set event
    // separately proves this recording executed even after the shared lease index was recycled.
    void Poll(uint64_t now) {
        for(uint32_t i=0;i<slots.size();++i){
            auto& slot=slots[i];if(!slot.pending)continue;
            if(!VkAudit::Ready(slot.lease))continue;
            slot.pending=false;
            if(!slot.sealed || vkGetEventStatus(device,executed[i])!=VK_EVENT_SET)continue;
            struct Query { uint64_t ticks,available; } result[2]{};
            const auto status=vkGetQueryPoolResults(device,pool,i*2,2,sizeof(result),result,sizeof(Query),
                VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if(status!=VK_SUCCESS || !result[0].available || !result[1].available)continue;
            const uint64_t mask=slot.bits==64?UINT64_MAX:((uint64_t(1)<<slot.bits)-1);
            const double ms=double((result[1].ticks-result[0].ticks)&mask)*period/1e6;
            if(std::isfinite(ms) && ms>=0 && ms<1000){
                lastMs=ms;lastRead=now;
                if(now>=nextLog){nextLog=now+5000;VkAudit::Write("event=nr_timing scope=encode_model_resolve ms=%.6f nonblocking=1",ms);}
            }
        }
    }
    int Begin(VkDevice d,VkPhysicalDevice physical,VkCommandBuffer cmd,VkAudit::Lease lease) {
        const uint64_t now=GetTickCount64();
        if(now<nextSample || unavailable)return -1;
        nextSample=now+100; // At most ten pairs/second; full slots simply skip this sample.
        if(pool)Poll(now);
        uint32_t bits=0;
        {
            std::lock_guard lock(VkAudit::trackingMutex);
            auto recording=VkAudit::recordings.find(cmd);
            auto functions=VkAudit::deviceFunctions.find(d);
            if(!lease.Valid() || recording==VkAudit::recordings.end() || functions==VkAudit::deviceFunctions.end() ||
                recording->second.device!=d || recording->second.epoch!=lease.epoch ||
                recording->second.family>=functions->second.families.size())return -1;
            bits=functions->second.families[recording->second.family].timestampValidBits;
        }
        if(!bits || bits>64){unavailable=true;return -1;}
        if(!attempted){
            attempted=true;device=d;
            VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);
            period=properties.limits.timestampPeriod;
            VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=uint32_t(slots.size())*2;
            unavailable=!std::isfinite(period)||period<=0||vkCreateQueryPool(d,&info,nullptr,&pool)!=VK_SUCCESS;
            if(!unavailable){
                VkEventCreateInfo eventInfo{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
                for(auto& event:executed)if(vkCreateEvent(d,&eventInfo,nullptr,&event)!=VK_SUCCESS){unavailable=true;break;}
            }
            VkAudit::Write("event=nr_timing_init available=%d slots=4 interval_ms=100",!unavailable);
        }
        if(unavailable || !pool || device!=d)return -1;
        for(uint32_t i=0;i<slots.size();++i)if(!slots[i].pending){
            if(vkResetEvent(d,executed[i])!=VK_SUCCESS){unavailable=true;return -1;}
            slots[i]={lease,bits,true,false};
            vkCmdResetQueryPool(cmd,pool,i*2,2);
            vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pool,i*2);
            return int(i);
        }
        return -1;
    }
    void End(VkCommandBuffer cmd,int slot){
        if(slot<0)return;
        vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,pool,uint32_t(slot)*2+1);
        vkCmdSetEvent(cmd,executed[size_t(slot)],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        slots[size_t(slot)].sealed=true;
    }
    std::optional<double> Value()const {
        return lastMs && GetTickCount64()-lastRead<=5000 ? lastMs : std::nullopt;
    }
};
}
