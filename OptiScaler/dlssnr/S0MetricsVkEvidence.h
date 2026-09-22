#pragma once
#include "D24VkDiagnostics.h"
#include "S0CaptureIdentity.h"
#include "S0MetricsControl.h"
#include "BuildProfile.h"
#include <atomic>

// Bounded CPU-only sidecar; Recording and CompletionSample layouts are unchanged.
// Lock order: caller trackingMutex (if any), then traceMutex. Never the reverse.
namespace S0MetricsVk {
struct SubmissionObservation {
    VkDevice device{};VkCommandBuffer cmd{};uint64_t epoch=0;
    VkQueue queue{};unsigned submissions=0,refs=0;
    bool mixed=false,failed=false,used=false;
};
inline std::mutex traceMutex;
inline std::array<SubmissionObservation,128> trace{};
inline std::atomic<unsigned> traceActive{0};
inline std::atomic<unsigned> lifecycleCount{0};
inline S0MetricsPolicy::Control control;
inline bool DiagnosticEnabled(){return DlssNr::BuildProfile::PixelCapture&&Config::Instance()->DlssNrDiagnostics.value_or_default()!=0;}
inline bool MarkerPresent(){
    const auto& id=S0CaptureIdentity::Current();if(!id.valid)return false;
    const auto path=Util::DllPath().parent_path()/(L"D18_S0_METRICS_"+std::to_wstring(id.pid)+L"_"+std::to_wstring(id.creation)+L".on");
    const auto attr=GetFileAttributesW(path.c_str());return attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_DIRECTORY);
}
inline void ControlEvent(const S0MetricsPolicy::Arm& a,const char* phase,const char* reason){
    const auto& id=S0CaptureIdentity::Current();
    DlssNr::VkAudit::Write("event=s0_metrics_control schema=1 api=Vulkan pid=%lu creation=%llu identity_valid=%d arm=%u phase=%s saved=%u target=32 reason=%s warmup=%u attempts=%u pending=%u",
        id.pid,id.creation,int(id.valid),a.id,phase,a.saved,reason,a.warmup,a.attempts,a.pending);
}
inline void ObserveGate(uint64_t now){if(!DlssNr::BuildProfile::Diagnostic)return;control.observer=ControlEvent;control.Gate(now,DiagnosticEnabled(),MarkerPresent(),S0CaptureIdentity::Current().valid);}
inline void Life(const char* action,uint64_t generation,VkDevice device,const char* reason,size_t leases,unsigned pending,int ready,int result=0){
    if(!DiagnosticEnabled())return;unsigned n=lifecycleCount.load();
    while(n<128&&!lifecycleCount.compare_exchange_weak(n,n+1)){}if(n>=128)return;
    const auto& id=S0CaptureIdentity::Current();
    DlssNr::VkAudit::Write("event=s0_lifecycle schema=1 api=Vulkan pid=%lu creation=%llu identity_valid=%d action=%s generation=%llu device=%p reason=%s lease_refs=%zu metric_pending=%u ready=%d result=%d ordinal=%u coverage=%s",
        id.pid,id.creation,int(id.valid),action,generation,(void*)device,reason,leases,pending,ready,result,n+1,n==127?"budget_exhausted":"observed");
}
inline bool Interest(VkDevice device,VkCommandBuffer cmd,uint64_t epoch){
    std::lock_guard lock(traceMutex);
    for(auto& s:trace)if(s.used&&s.device==device&&s.cmd==cmd&&s.epoch==epoch){++s.refs;return true;}
    for(auto& s:trace)if(!s.used){s={device,cmd,epoch,VK_NULL_HANDLE,0,1,false,false,true};++traceActive;return true;}return false;
}
inline SubmissionObservation Read(VkDevice device,VkCommandBuffer cmd,uint64_t epoch){
    std::lock_guard lock(traceMutex);for(const auto& s:trace)if(s.used&&s.device==device&&s.cmd==cmd&&s.epoch==epoch)return s;return {};
}
inline void Release(VkDevice device,VkCommandBuffer cmd,uint64_t epoch){
    std::lock_guard lock(traceMutex);for(auto& s:trace)if(s.used&&s.device==device&&s.cmd==cmd&&s.epoch==epoch){if(--s.refs==0){s={};--traceActive;}return;}
}
inline void Submitted(VkDevice device,VkCommandBuffer cmd,uint64_t epoch,VkQueue queue,VkResult result){
    if(!traceActive.load())return;
    std::lock_guard lock(traceMutex);for(auto& s:trace)if(s.used&&s.device==device&&s.cmd==cmd&&s.epoch==epoch){
        if(s.queue&&s.queue!=queue)s.mixed=true;s.queue=queue;if(s.submissions<UINT32_MAX)++s.submissions;s.failed|=result!=VK_SUCCESS;return;
    }
}
inline void TrackingTeardown(VkDevice device,bool idleCalled,VkResult idle,size_t leaseRefs){
    Life("tracking_teardown_snapshot",0,device,idleCalled?"existing_tracking_idle_call":"tracking_no_pending_fence_wait",leaseRefs,0,-1,int(idle));
}
}
