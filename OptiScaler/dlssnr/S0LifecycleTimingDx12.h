#pragma once
// S0-only diagnostics. Private query resources never enter the normal NR policy.
#include "S0TimingPolicy.h"
#ifdef D18_S0_TIMING_HOST_TEST
#include <S0TimingTestStubs.h>
#else
#include <d3d12.h>
#include <wrl/client.h>
#include <Config.h>
#include <Util.h>
#endif
#include <array>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <string>

namespace DlssNr::S0Timing {
using Microsoft::WRL::ComPtr;
inline bool (*resetCoverage)(ID3D12GraphicsCommandList*) = nullptr;
struct Identity {
    DWORD pid = GetCurrentProcessId(); uint64_t creation = 0; bool valid = false;
    Identity() {
        FILETIME c{}, e{}, k{}, u{};
        valid = GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) != 0;
        if (valid) creation = (uint64_t(c.dwHighDateTime) << 32) | c.dwLowDateTime;
        valid = valid && pid != 0 && creation != 0;
    }
};
struct Meta {
    uint64_t frame = 0, generation = 0, feature = 0;
    uint32_t phf = 0, width = 0, height = 0;
    bool capture = false, menu = false, reset = false;
};
struct Token { uint64_t id = 0; explicit operator bool() const { return id != 0; } };
struct Batch { std::array<Token,4> tokens{}; unsigned count = 0; };
struct Slot {
    Token token; Meta meta; RecordingProof proof;
    unsigned arm = 0; uint64_t deadline = 0, fenceValue = 0, frequency = 0;
    bool ended = false, reported = false, ambiguousQueue = false;
    const char* reason = "recording";
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12QueryHeap> query;
    ComPtr<ID3D12Resource> readback;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
};
class Manager {
    Identity identity_;
    std::mutex mutex_;
    std::array<Slot,4> slots_{};
    std::array<ArmBudget,4> arms_{};
    std::array<bool,4> coverageReported_{};
    unsigned armCount_ = 0, totalSaved_ = 0, lifecycleCount_ = 0;
    uint64_t nextToken_ = 0, nextEpoch_ = 0, lastMarkerPoll_ = 0;
    bool markerWasPresent_ = false, diagnostics_ = false, shutdown_ = false;
    std::atomic<bool> fastActive_{false};
    Slot* Find(Token token) {
        for (auto& s:slots_) if (token.id && s.token.id == token.id) return &s;
        return nullptr;
    }
    unsigned Pending() const { unsigned n=0; for (const auto& s:slots_) n += bool(s.token); return n; }
    void Control(unsigned arm, const char* phase, const char* reason) {
        const unsigned saved = arm && arm <= armCount_ ? arms_[arm-1].saved : 0;
        LOG_INFO("event=s0_metrics_control schema=1 api=DX12 pid={} creation={} identity_valid={} arm={} phase={} saved={} target=32 reason={}",
            identity_.pid, identity_.creation, identity_.valid?1:0, arm, phase, saved, reason);
    }
    void RefreshFast() {
        fastActive_.store(Pending()!=0 || (armCount_ && arms_[armCount_-1].active),std::memory_order_release);
    }
    void ReportDiscard(Slot& s,const char* reason) {
        if (s.reported) return; s.reported = true;
        LOG_INFO("event=s0_gpu_timing schema=1 api=DX12 pid={} creation={} identity_valid={} arm={} sample={} frame={} generation={} feature={} phf={} width={} height={} phase=discarded reason={} recording_epoch={} sealed={} submission_count={} queue={} completion=not_accepted",
            identity_.pid,identity_.creation,identity_.valid?1:0,s.arm,s.token.id,s.meta.frame,s.meta.generation,s.meta.feature,s.meta.phf,s.meta.width,s.meta.height,reason,s.proof.epoch,s.proof.sealed?1:0,s.proof.submissions,reinterpret_cast<uint64_t>(s.queue.Get()));
    }
    void ServiceLocked(uint64_t now) {
        for (auto& s:slots_) {
            if (!s.token) continue;
            if (now >= s.deadline) { s.proof.invalid = true; ReportDiscard(s,"deadline_pending_retained"); }
            if (s.ambiguousQueue || s.proof.signalFailed) continue; // GPU lifetime remains unknown: keep resources pinned.
            const uint64_t done = s.fence && s.proof.submissions ? s.fence->GetCompletedValue() : 0;
            const bool reached = s.proof.submissions && done != UINT64_MAX && done >= s.fenceValue;
            if (!s.proof.SafeToRetire(reached)) continue;
            auto& arm=arms_[s.arm-1];
            const bool canRead = s.ended && s.proof.UniqueComplete(reached) && s.frequency>0 &&
                now < s.deadline && arm.saved < 32 && totalSaved_ < 128 && !shutdown_;
            if (canRead) {
                const D3D12_RANGE range{0,6*sizeof(uint64_t)}; void* mapped = nullptr;
                const HRESULT hr=s.readback->Map(0,&range,&mapped);
                uint64_t ticks[6]{};
                if (SUCCEEDED(hr) && mapped) {
                    std::memcpy(ticks,mapped,sizeof(ticks));const D3D12_RANGE none{0,0};s.readback->Unmap(0,&none);
                    bool ordered=ticks[0]!=0;for(unsigned i=0;i<6;++i)ordered=ordered && ticks[i]!=UINT64_MAX;
                    for(unsigned i=1;i<6;++i)ordered=ordered && ticks[i]>=ticks[i-1];
                    const bool bounded=ordered && ticks[5]-ticks[0]<s.frequency;
                    if (bounded) {
                        LOG_INFO("event=s0_gpu_timing schema=1 api=DX12 pid={} creation={} identity_valid={} arm={} sample={} frame={} generation={} feature={} phf={} width={} height={} phase=complete reason=ready domain=gpu_ticks ticks0={} ticks1={} ticks2={} ticks3={} ticks4={} ticks5={} frequency_hz={} timestamp_bits=64 recording_epoch={} sealed=1 submission_count=1 fence_completed=1 queue={} reset=0 capture_active=0 menu_visible=0 completion=dx12_sealed_recording_all_submission_fences",
                            identity_.pid,identity_.creation,identity_.valid?1:0,s.arm,s.token.id,s.meta.frame,s.meta.generation,s.meta.feature,s.meta.phf,s.meta.width,s.meta.height,ticks[0],ticks[1],ticks[2],ticks[3],ticks[4],ticks[5],s.frequency,s.proof.epoch,reinterpret_cast<uint64_t>(s.queue.Get()));
                        ++arm.saved;++totalSaved_;s.reported=true;
                        if(arm.saved==32){arm.active=false;Control(s.arm,"complete","target_reached");}
                    } else ReportDiscard(s,"invalid_timestamp_order_or_span");
                } else ReportDiscard(s,"query_map_failed");
            } else ReportDiscard(s,s.proof.submissions>1?"multiple_submissions":!s.proof.submissions?"reset_discarded_unsubmitted":arm.saved>=32?"target_already_complete":s.reason);
            s=Slot{}; // Only a sealed recording plus all observed fences authorizes release.
        }
        if(armCount_) {
            auto& arm=arms_[armCount_-1];
            if(arm.active && (arm.Expired(now)||arm.attempts>=256)) {
                arm.active=false;Control(armCount_,"partial",arm.Expired(now)?"deadline":"attempt_limit");
            }
        }
        RefreshFast();
    }
public:
    bool FastActive() const { return fastActive_.load(std::memory_order_acquire); }
    void Poll(bool diagnostics) {
        if(!diagnostics && !FastActive())return;
        std::lock_guard lock(mutex_);diagnostics_=diagnostics;
        const auto now=GetTickCount64();ServiceLocked(now);
        if(lastMarkerPoll_ && now-lastMarkerPoll_<100)return;lastMarkerPoll_=now;
        bool exists=false;
        if(identity_.valid && diagnostics && !shutdown_) {
            const auto name=L"D18_S0_METRICS_"+std::to_wstring(identity_.pid)+L"_"+std::to_wstring(identity_.creation)+L".on";
            const auto path=Util::DllPath().parent_path()/name;
            const auto attr=GetFileAttributesW(path.c_str());exists=attr!=INVALID_FILE_ATTRIBUTES && !(attr&FILE_ATTRIBUTE_DIRECTORY);
        }
        if(exists && !markerWasPresent_ && armCount_<4 && totalSaved_<128) {
            ++armCount_;arms_[armCount_-1].Start(now);Control(armCount_,"armed","process_marker_rising_edge");
        }
        if(!exists && markerWasPresent_ && armCount_ && arms_[armCount_-1].active) {
            arms_[armCount_-1].active=false;Control(armCount_,"stopped",diagnostics?"marker_removed":"diagnostics_off");
            for(auto& s:slots_)if(s.token && s.arm==armCount_){s.proof.invalid=true;s.reason="request_stopped";}
        }
        markerWasPresent_=exists;RefreshFast();
    }
    Token Begin(ID3D12Device* device,ID3D12GraphicsCommandList* list,const Meta& meta,bool eligible,bool resetHookCovered) {
        if(!FastActive())return {};
        std::lock_guard lock(mutex_);const auto now=GetTickCount64();ServiceLocked(now);
        if(!identity_.valid || !diagnostics_ || shutdown_ || !armCount_ || !device || !list)return {};
        auto& arm=arms_[armCount_-1];
        if(meta.reset || arm.warmupGeneration!=meta.generation){arm.warmup=0;arm.warmupGeneration=meta.generation;}
        if(!resetHookCovered && !coverageReported_[armCount_-1]){coverageReported_[armCount_-1]=true;Control(armCount_,"skipped","reset_hook_unavailable");}
        if(!eligible || meta.capture || meta.menu || meta.reset || !resetHookCovered || !meta.frame || !meta.generation || !meta.feature || !meta.width || !meta.height ||
            list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return {};
        // One timestamp set per command-list recording. Reusing the list is allowed only after Reset seals this one.
        for(const auto& s:slots_)if(s.token && s.list.Get()==list && !s.proof.sealed)return {};
        if(!arm.Admit(now,true,Pending(),totalSaved_))return {};
        Slot* s=nullptr;for(auto& candidate:slots_)if(!candidate.token){s=&candidate;break;}if(!s)return {};
        const D3D12_QUERY_HEAP_DESC q{D3D12_QUERY_HEAP_TYPE_TIMESTAMP,6,0};
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=6*sizeof(uint64_t);rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device->CreateQueryHeap(&q,IID_PPV_ARGS(&s->query))) ||
            FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s->readback))) ||
            FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s->fence)))) {
            *s=Slot{};Control(armCount_,"skipped","query_allocation_failed");return {};
        }
        void* initial=nullptr;const D3D12_RANGE noRead{0,0};
        if(FAILED(s->readback->Map(0,&noRead,&initial))||!initial){*s=Slot{};Control(armCount_,"skipped","query_initialization_map_failed");return {};}
        std::memset(initial,0xff,6*sizeof(uint64_t));const D3D12_RANGE wrote{0,6*sizeof(uint64_t)};s->readback->Unmap(0,&wrote);
        s->token.id=++nextToken_;s->proof.epoch=++nextEpoch_;s->list=list;s->device=device;s->meta=meta;s->arm=armCount_;s->deadline=arm.began+30000;
        RefreshFast();return s->token;
    }
    void Stamp(Token token,ID3D12GraphicsCommandList* list,unsigned index) {
        if(!token)return;std::lock_guard lock(mutex_);auto* s=Find(token);
        if(!s || s->list.Get()!=list || s->proof.sealed || s->proof.invalid || index>=6)return;
        if(s->proof.writtenMask & (1u<<index)){s->proof.invalid=true;s->reason="duplicate_stage_timestamp";return;}
        list->EndQuery(s->query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,index);s->proof.writtenMask|=1u<<index;
    }
    void End(Token token,ID3D12GraphicsCommandList* list,bool success,const char* reason) {
        if(!token)return;std::lock_guard lock(mutex_);auto* s=Find(token);if(!s || s->ended)return;
        s->ended=true;s->reason=reason;
        if(!success || s->proof.writtenMask!=63 || s->list.Get()!=list || s->proof.sealed){s->proof.invalid=true;return;}
        list->ResolveQueryData(s->query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,6,s->readback.Get(),0);
    }
    void Cancel(Token token,const char* reason) {
        if(!token)return;std::lock_guard lock(mutex_);auto* s=Find(token);
        if(s && !s->ended){s->ended=true;s->proof.invalid=true;s->reason=reason;arms_[s->arm-1].warmup=0;}
    }
    Batch BeforeSubmit(ID3D12CommandQueue*,UINT count,ID3D12CommandList* const* lists) {
        Batch batch{};if(!FastActive() || !lists)return batch;
        std::lock_guard lock(mutex_);
        for(auto& s:slots_) {
            if(!s.token || s.proof.sealed)continue;
            unsigned occurrences=0;for(UINT i=0;i<count;++i)occurrences+=lists[i]==s.list.Get();
            if(occurrences){s.proof.BeforeExecute();batch.tokens[batch.count++]=s.token;
                if(occurrences>1){s.proof.invalid=true;s.reason="duplicate_list_in_submit_batch";}}
        }
        return batch;
    }
    void AfterSubmit(ID3D12CommandQueue* queue,const Batch& batch) {
        if(!batch.count)return;std::lock_guard lock(mutex_);
        for(unsigned i=0;i<batch.count;++i) {
            auto* s=Find(batch.tokens[i]);if(!s)continue;
            if(!queue || (s->queue && s->queue.Get()!=queue)) {
                s->ambiguousQueue=true;s->proof.AfterExecute(false);ReportDiscard(*s,"queue_identity_ambiguous_retained");continue;
            }
            ComPtr<ID3D12Device> qd;ComPtr<IUnknown> qi,di;
            if(FAILED(queue->GetDevice(IID_PPV_ARGS(&qd)))||FAILED(qd.As(&qi))||FAILED(s->device.As(&di))||qi.Get()!=di.Get()||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT){
                s->ambiguousQueue=true;s->proof.AfterExecute(false);ReportDiscard(*s,"queue_device_or_type_mismatch_retained");continue;
            }
            if(!s->queue){s->queue=queue;if(FAILED(queue->GetTimestampFrequency(&s->frequency))||!s->frequency){s->proof.invalid=true;s->reason="frequency_unavailable";}}
            const auto signal=queue->Signal(s->fence.Get(),++s->fenceValue);s->proof.AfterExecute(SUCCEEDED(signal));
            if(FAILED(signal))ReportDiscard(*s,"signal_failed_retained");
        }
        RefreshFast();
    }
    void SuccessfulReset(ID3D12GraphicsCommandList* list) {
        if(!FastActive() || !list)return;std::lock_guard lock(mutex_);
        for(auto& s:slots_)if(s.token && s.list.Get()==list && !s.proof.sealed)s.proof.SuccessfulReset();
        // Do not Map here: a concurrent post-Execute callback may still need to attach its exact snapshot.
    }
    void Lifecycle(const char* action,const char* reason,uint64_t generation,uint64_t feature,uint32_t width,uint32_t height,
                   const char* proof="cpu_observed",uint64_t value=0,bool featureUnknown=false,bool valueKnown=true) {
        if(Config::Instance()->DlssNrDiagnostics.value_or_default()==0)return;
        std::lock_guard lock(mutex_);if(lifecycleCount_>=128)return;++lifecycleCount_;
        LOG_INFO("event=s0_lifecycle schema=1 api=DX12 pid={} creation={} identity_valid={} action={} reason={} generation={} feature={} width={} height={} proof={} value={}",
            identity_.pid,identity_.creation,identity_.valid?1:0,action,reason,generation?std::to_string(generation):"unknown",
            featureUnknown?"unknown":std::to_string(feature),width?std::to_string(width):"unknown",height?std::to_string(height):"unknown",
            proof,valueKnown?std::to_string(value):"unknown");
    }
    void Shutdown() {
        std::lock_guard lock(mutex_);shutdown_=true;
        for(unsigned i=0;i<armCount_;++i)if(arms_[i].active){arms_[i].active=false;Control(i+1,"stopped","shutdown");}
        for(auto& s:slots_)if(s.token){s.proof.invalid=true;ReportDiscard(s,"shutdown_pending_retained");}
        ServiceLocked(GetTickCount64()); // Safe slots retire; uncertain slots stay in intentionally process-lifetime storage.
    }
};
inline Manager& Get() { static auto* manager=new Manager; return *manager; }
}
