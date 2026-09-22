#pragma once
#include "D24VkTracking.h"
#include "S0MetricsVkEvidence.h"
#include <cmath>
#include "S0PrecisionGate.h"

namespace DlssNr {
// Dedicated diagnostic query resources. The existing NativeTimingVk UI meter is
// byte-identical. No extra submit, fence, barrier, image, or CPU wait is inserted.
struct S0GpuTimingVk {
    struct Context {
        unsigned arm=0;uint64_t sample=0,frame=0,generation=0,run=0;
        unsigned width=0,height=0,phf=0;
        void* feature=nullptr;
        bool reset=false,capture=false,menu=false,phfKnown=false;
        S0Precision::Evidence precision{};
        unsigned long pid=0;unsigned long long creation=0;bool identity=false;
    };
    struct Slot {
        VkAudit::Lease lease{};VkCommandBuffer cmd{};Context context{};
        unsigned bits=0,mask=0;bool pending=false,sealed=false,aborted=false;
        const char* abortReason="recording_incomplete";
    };
    inline static uint64_t nextSample=0;
    VkDevice device{};VkQueryPool pool{};std::array<VkEvent,4> events{};std::array<Slot,4> slots{};
    double period=0;bool attempted=false,unavailable=false,ownerSafe=false;
    unsigned Pending()const{unsigned n=0;for(const auto& s:slots)n+=s.pending;return n;}
    ~S0GpuTimingVk(){
        // Static process/DLL teardown is not itself an observed safe retirement.
        // Owner paths below explicitly authorize destruction of diagnostic handles.
        if(!ownerSafe)return;
        for(auto event:events)if(event)vkDestroyEvent(device,event,nullptr);
        if(pool)vkDestroyQueryPool(device,pool,nullptr);
    }
    void Outcome(Slot& s,const char* phase,const char* reason){
        const auto& c=s.context;
        const auto phf=c.phfKnown?std::to_string(c.phf):std::string("unknown");
        VkAudit::Write("event=s0_gpu_timing schema=1 api=Vulkan pid=%lu creation=%llu identity_valid=%d arm=%u sample=%llu frame=%llu generation=%llu run=%llu feature=%p phf=%s phf_known=%d width=%u height=%u phase=%s reason=%s domain=gpu_ticks recording_epoch=%llu reached_mask=%u sealed=%d reset=%d capture_active=%d menu_visible=%d",
            c.pid,c.creation,int(c.identity),c.arm,c.sample,c.frame,c.generation,c.run,c.feature,phf.c_str(),int(c.phfKnown),c.width,c.height,phase,reason,s.lease.epoch,s.mask,int(s.sealed),int(c.reset),int(c.capture),int(c.menu));
    }
    void Release(Slot& s,bool complete){
        S0MetricsVk::Release(device,s.cmd,s.lease.epoch);S0MetricsVk::control.Result(s.context.arm,complete);s=Slot{};
    }
    bool Init(VkDevice d,VkPhysicalDevice physical){
        if(attempted)return !unavailable&&device==d&&pool;
        attempted=true;device=d;
        VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);period=props.limits.timestampPeriod;
        if(!std::isfinite(period)||period<=0){unavailable=true;return false;}
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=24;
        if(vkCreateQueryPool(d,&info,nullptr,&pool)!=VK_SUCCESS){unavailable=true;return false;}
        VkEventCreateInfo ei{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
        for(auto& e:events)if(vkCreateEvent(d,&ei,nullptr,&e)!=VK_SUCCESS){unavailable=true;break;}
        if(unavailable){for(auto& e:events)if(e){vkDestroyEvent(d,e,nullptr);e={};}vkDestroyQueryPool(d,pool,nullptr);pool={};}
        return !unavailable;
    }
    int Begin(VkDevice d,VkPhysicalDevice physical,VkCommandBuffer cmd,VkAudit::Lease lease,Context context){
        S0MetricsVk::ObserveGate(GetTickCount64());
        unsigned arm=S0MetricsVk::control.Reserve(GetTickCount64(),!context.reset&&!context.capture&&!context.menu);
        if(!arm)return -1;
        context.arm=arm;context.sample=++nextSample;const auto& id=S0CaptureIdentity::Current();
        context.pid=id.pid;context.creation=id.creation;context.identity=id.valid;
        Slot rejected;rejected.context=context;rejected.lease=lease;rejected.cmd=cmd;
        auto reject=[&](const char* reason){Outcome(rejected,"skipped",reason);S0MetricsVk::control.Result(arm,false);return -1;};
        if(!context.identity)return reject("identity_unavailable");
        unsigned bits=0;
        {std::lock_guard lock(VkAudit::trackingMutex);auto r=VkAudit::recordings.find(cmd);auto f=VkAudit::deviceFunctions.find(d);
            if(!lease.Valid()||r==VkAudit::recordings.end()||f==VkAudit::deviceFunctions.end()||r->second.device!=d||r->second.epoch!=lease.epoch||r->second.family>=f->second.families.size())return reject("recording_identity_unavailable");
            bits=f->second.families[r->second.family].timestampValidBits;}
        if(!bits||bits>64)return reject("timestamp_bits_unavailable");
        if(!Init(d,physical))return reject("diagnostic_query_resources_unavailable");
        for(unsigned i=0;i<slots.size();++i)if(!slots[i].pending){
            if(vkResetEvent(d,events[i])!=VK_SUCCESS)return reject("event_reset_failed");
            if(!S0MetricsVk::Interest(d,cmd,lease.epoch))return reject("submission_observer_capacity");
            slots[i]={lease,cmd,context,bits,0,true,false,false,"recording_incomplete"};
            vkCmdResetQueryPool(cmd,pool,i*6,6);Mark(cmd,int(i),0);return int(i);
        }
        return reject("state_pending_slots_full");
    }
    void Mark(VkCommandBuffer cmd,int index,unsigned point){
        if(index<0)return;auto& s=slots[size_t(index)];
        if(!s.pending||s.aborted)return;
        if(cmd!=s.cmd||point>=6||s.mask!=((1u<<point)-1)){s.aborted=true;s.abortReason="timestamp_recording_order_mismatch";return;}
        // Equal timestamp stages make these explicit command-completion boundary
        // intervals; they are not isolated shader self-time or hidden NR queues.
        vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,pool,unsigned(index)*6+point);s.mask|=1u<<point;
        if(point==5){vkCmdSetEvent(cmd,events[size_t(index)],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);s.sealed=true;}
    }
    void SetPrecision(int index,const S0Precision::Evidence& e){if(index>=0)slots[size_t(index)].context.precision=e;}
    void SetPhf(int index,unsigned phf){if(index<0)return;auto& c=slots[size_t(index)].context;c.phf=phf;c.phfKnown=true;}
    void Abort(int index,const char* reason){if(index<0)return;auto& s=slots[size_t(index)];s.aborted=true;s.abortReason=reason;}
    void Poll(){
        for(unsigned i=0;i<slots.size();++i){auto& s=slots[i];if(!s.pending||!VkAudit::Ready(s.lease))continue;
            if(s.aborted||!s.sealed||s.mask!=63||!s.context.phfKnown||!s.context.feature||!s.context.precision.known){Outcome(s,"partial",s.aborted?s.abortReason:"recording_incomplete");Release(s,false);continue;}
            const auto observed=S0MetricsVk::Read(device,s.cmd,s.lease.epoch);
            if(!observed.used||observed.submissions!=1||observed.mixed||observed.failed||!observed.queue){Outcome(s,"partial","single_queue_submission_not_proven");Release(s,false);continue;}
            if(vkGetEventStatus(device,events[i])!=VK_EVENT_SET){Outcome(s,"partial","GPU_event_not_set");Release(s,false);continue;}
            struct Query{uint64_t ticks,available;} result[6]{};
            const auto status=vkGetQueryPoolResults(device,pool,i*6,6,sizeof(result),result,sizeof(Query),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            unsigned available=0;for(unsigned j=0;j<6;++j)if(result[j].available)available|=1u<<j;
            if(status!=VK_SUCCESS||available!=63){Outcome(s,"partial","query_availability_incomplete");Release(s,false);continue;}
            const uint64_t mask=s.bits==64?UINT64_MAX:((uint64_t(1)<<s.bits)-1);long double span=0;
            for(unsigned j=1;j<6;++j)span+=((result[j].ticks-result[j-1].ticks)&mask);
            const double totalMs=double(span*period/1e6);
            if(!std::isfinite(totalMs)||totalMs<0||totalMs>=1000){Outcome(s,"partial","timestamp_span_invalid");Release(s,false);continue;}
            const auto& c=s.context;
            VkAudit::Write("event=s0_gpu_timing schema=1 api=Vulkan pid=%lu creation=%llu identity_valid=%d arm=%u sample=%llu frame=%llu generation=%llu run=%llu feature=%p phf=%u width=%u height=%u phase=complete reason=sample_ready domain=gpu_ticks ticks0=%llu ticks1=%llu ticks2=%llu ticks3=%llu ticks4=%llu ticks5=%llu period_ns=%.17g timestamp_bits=%u completion=retired_lease_event_query_available recording_epoch=%llu queue=%p submission_count=%u mixed_queue=0 lease_retired=1 event_set=1 availability_mask=63 sealed=1 reset=%d capture_active=%d menu_visible=%d timestamp_stage=bottom_of_pipe encode_scope=prepare_and_optional_prefilter interval_scope=command_completion_boundaries total_span_ms=%.9g precision_schema=1 precision_variant=%s precision_requested=%d precision_reason=%s precision_pipeline=%p precision_cache_key=%llu precision_base_spirv_sha256=%s precision_sample_source=%u precision_sample_model=%u precision_target=%u precision_keep=%u precision_ratio_x=%.9g precision_ratio_y=%.9g",
                c.pid,c.creation,int(c.identity),c.arm,c.sample,c.frame,c.generation,c.run,c.feature,c.phf,c.width,c.height,
                result[0].ticks,result[1].ticks,result[2].ticks,result[3].ticks,result[4].ticks,result[5].ticks,period,s.bits,s.lease.epoch,(void*)observed.queue,observed.submissions,int(c.reset),int(c.capture),int(c.menu),totalMs,
                S0Precision::Name(c.precision.variant),int(c.precision.requested),c.precision.reason,(void*)c.precision.pipeline,c.precision.key,S0Precision::Sha(c.precision.variant),
                c.precision.sourceFormat,c.precision.modelFormat,c.precision.targetFormat,c.precision.keepFormat,c.precision.ratioX,c.precision.ratioY);
            Release(s,true);
        }
    }
    void OwnerRetired(const char* reason){
        // Called only after existing lease/capture retirement or existing device
        // idle/device-lost policy. No new completion is inferred from device loss.
        for(auto& s:slots)if(s.pending){Outcome(s,"partial",reason);Release(s,false);}ownerSafe=true;
    }
};
}
