#pragma once
#include <array>
#include <atomic>
#include <string>
#include <dlssnr/GuideResourcesVk.h>
#include "S0CaptureIdentity.h"
namespace S0GuideCoverage {
inline std::array<std::atomic<unsigned>,4> resolutions{},hits{},successes{};
inline std::atomic<unsigned> emittedMask{0};
inline constexpr unsigned Limit=1000000;
inline void Increment(std::atomic<unsigned>& value){auto n=value.load();while(n<Limit&&!value.compare_exchange_weak(n,n+1)){};}
inline int Index(const char* name){if(!strcmp(name,"vkCreateImage"))return 0;if(!strcmp(name,"vkCreateImageView"))return 1;if(!strcmp(name,"vkDestroyImage"))return 2;if(!strcmp(name,"vkDestroyImageView"))return 3;return -1;}
inline void Once(unsigned bit,const char* stage,unsigned index,bool original,bool success){
    if(emittedMask.fetch_or(1u<<bit)&(1u<<bit))return;
    const auto& id=S0CaptureIdentity::Current();
    DlssNr::VkAudit::Write("event=s0_guide_hook_coverage api=Vulkan pid=%lu creation=%llu identity_valid=%d stage=%s kind=%u enabled=1 original_present=%d success=%d counter_limit=%u coverage=diagnostics_enabled_calls_only",
        id.pid,id.creation,int(id.valid),stage,index,int(original),int(success),Limit);
}
inline void Resolved(const char* name,bool original){
    if(!DlssNr::GuideResourcesVk::Enabled())return;const int i=Index(name);if(i<0)return;
    Increment(resolutions[i]);Once(unsigned(i),"resolver",unsigned(i),original,original);
}
inline void Hit(unsigned index,bool success){
    if(!DlssNr::GuideResourcesVk::Enabled()||index>=4)return;
    Increment(hits[index]);if(success)Increment(successes[index]);Once(4+index,"hook",index,true,success);
}
inline std::string Json(){
    std::string out="{\"schema\":\"d18-guide-hook-coverage-v1\",\"tracking_enabled_now\":";
    out+=DlssNr::GuideResourcesVk::Enabled()?"true":"false";
    out+=",\"observation_scope\":\"diagnostics_enabled_calls_only\",\"off_calls_observed\":false,\"counter_limit\":1000000,\"kinds\":[";
    for(unsigned i=0;i<4;++i){if(i)out+=",";out+="{\"kind\":"+std::to_string(i)+",\"resolver_calls\":"+std::to_string(resolutions[i].load())+",\"hook_calls\":"+std::to_string(hits[i].load())+",\"successful_calls\":"+std::to_string(successes[i].load())+"}";}
    return out+"]}";
}
}
