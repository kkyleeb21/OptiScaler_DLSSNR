#pragma once
#include <d3d12.h>
#include "SrQueuePolicy.h"
#include <wrl/client.h>
namespace DlssNr { bool EnsureNativeSubmissionObserver(ID3D12Device* device); }
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <string>

namespace DlssNr::Submission {
struct Ticket {
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    std::atomic<bool> submitted{false};
    bool Complete() const {
        if (!submitted.load()) return false;
        const auto value = fence->GetCompletedValue();
        return value != UINT64_MAX && value >= 1;
    }
};
using Token = std::shared_ptr<Ticket>;
inline ID3D12CommandQueue* owner = nullptr;
inline std::mutex mutex;
inline std::unordered_map<ID3D12CommandList*,std::vector<Token>> pending;
// Bounded observation only. Raw addresses are never dereferenced or used to authorize NR.
struct Seen {uintptr_t queue=0; ULONGLONG tick=0;};
inline std::unordered_map<ID3D12CommandList*,Seen> observed;
// Watch only lists presented at the native SR seam. COM pins prevent address reuse.
// History selects a candidate; only the later actual Execute + Signal completes its ticket.
struct SrList {
    Microsoft::WRL::ComPtr<ID3D12CommandList> pin;
    Microsoft::WRL::ComPtr<IUnknown> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    ULONGLONG tick=0;
    bool ambiguous=false;
    uint64_t serial=0;
};
inline std::unordered_map<ID3D12CommandList*,SrList> srLists;
// Recent actual Execute observations are independently pinned before a list is
// first recognized as SR. Eviction forgets evidence; it never authorizes reuse.
inline constexpr size_t MaxRecentExecutions=512;
inline std::unordered_map<ID3D12CommandList*,SrList> recentExecutions;
inline uint64_t executionSerial=0;
inline void ClearRecentExecutionHistory(){std::lock_guard lock(mutex);recentExecutions.clear();}
inline void ObserveExecution(SrList& entry,ID3D12CommandList* list,ID3D12CommandQueue* queue) {
    if(!list || !queue)return;
    if(!entry.pin)entry.pin=list;
    entry.tick=GetTickCount64();entry.serial=++executionSerial;
    if(entry.device && entry.queue.Get()==queue)return; // immutable pinned device identity
    Microsoft::WRL::ComPtr<ID3D12Device> ld,qd;
    Microsoft::WRL::ComPtr<IUnknown> li,qi;
    if(list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT || queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
       FAILED(list->GetDevice(IID_PPV_ARGS(&ld))) || FAILED(queue->GetDevice(IID_PPV_ARGS(&qd))) ||
       FAILED(ld.As(&li)) || FAILED(qd.As(&qi)) || li.Get()!=qi.Get() ||
       (entry.device && entry.device.Get()!=li.Get())) {entry.ambiguous=true;return;}
    entry.device=li;
    if(entry.queue && entry.queue.Get()!=queue)entry.ambiguous=true;
    if(!entry.queue)entry.queue=queue;
}
inline void RememberExecution(ID3D12CommandList* list,ID3D12CommandQueue* queue) {
    if(!list || !queue || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return;
    auto found=recentExecutions.find(list);
    if(found==recentExecutions.end()){
        if(recentExecutions.size()>=MaxRecentExecutions){
            auto oldest=recentExecutions.begin();
            for(auto it=recentExecutions.begin();it!=recentExecutions.end();++it)
                if(it->second.serial<oldest->second.serial)oldest=it;
            recentExecutions.erase(oldest); // CPU identity pins only; not NR lifetime tickets
        }
        found=recentExecutions.emplace(list,SrList{}).first;
    }
    ObserveExecution(found->second,list,queue);
}
inline uint64_t executeCalls=0, ownerCalls=0, diagnosticSkips=0;
inline std::string lastBlock;
inline void ExplainLocked(ID3D12CommandQueue* candidate,ID3D12GraphicsCommandList* list,const char* reason) {
    lastBlock=reason;
    const auto sample=++diagnosticSkips;
    if(sample>3 && sample%300!=0) return;
    const auto found=observed.find(list);
    const Seen seen=found==observed.end()?Seen{}:found->second;
    LOG_INFO("D18 queue evidence: reason={} sample={} candidate={} owner={} list={} observed-queue={} observed-tick={} now={} execute-calls={} owner-calls={} pending={}",
        reason,sample,(void*)candidate,(void*)owner,(void*)list,(void*)seen.queue,seen.tick,GetTickCount64(),executeCalls,ownerCalls,pending.size());
}
inline void Explain(ID3D12CommandQueue* queue,ID3D12GraphicsCommandList* list,const char* reason) {
    std::lock_guard lock(mutex);ExplainLocked(queue,list,reason);
}
inline std::string LastBlock(){std::lock_guard lock(mutex);return lastBlock;}
inline void NotifySubmitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    std::vector<Token> batch;
    {
        std::lock_guard lock(mutex);
        ++executeCalls;if(queue==owner) ++ownerCalls;
        for(UINT i=0;i<count;++i) {
            if(auto sr=srLists.find(lists[i]);sr!=srLists.end()) {
                ObserveExecution(sr->second,lists[i],queue);
            } else RememberExecution(lists[i],queue);
            if(!observed.contains(lists[i]) && observed.size()>=2048) observed.clear();
            observed[lists[i]]={reinterpret_cast<uintptr_t>(queue),GetTickCount64()};
        }
        for (UINT i=0;queue==owner && i<count;++i) {
            auto it=pending.find(lists[i]);
            if(it!=pending.end()) { batch.insert(batch.end(),it->second.begin(),it->second.end()); pending.erase(it); }
        }
    }
    // Signal only AFTER the actual batch submission, never during CPU command recording.
    for(auto& ticket:batch)
        if(SUCCEEDED(queue->Signal(ticket->fence.Get(),1))) ticket->submitted.store(true);
}
inline bool EnsureHookLocked(ID3D12CommandQueue* queue){
    if(!queue)return false;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    return SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))) && DlssNr::EnsureNativeSubmissionObserver(device.Get());
}
inline Microsoft::WRL::ComPtr<ID3D12CommandQueue> ResolveSrQueue(
        ID3D12CommandQueue* bootstrap,ID3D12GraphicsCommandList* list,
        const char** rejection=nullptr,bool* promoted=nullptr) {
    if(promoted)*promoted=false;
    if(rejection)*rejection="queue_unknown_list";
    if(!list || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT){
        if(rejection)*rejection="queue_list_not_direct";return {};
    }
    std::lock_guard lock(mutex);
    if(!EnsureHookLocked(bootstrap)) {
        if(rejection)*rejection="queue_observer_unavailable";
        ExplainLocked(bootstrap,list,"Cannot install SR queue observer");return {};
    }
    auto it=srLists.find(list);
    bool inherited=false;
    if(it==srLists.end()) {
        if(srLists.size()>=256) {if(rejection)*rejection="queue_sr_list_limit";ExplainLocked(bootstrap,list,"SR list observation limit");return {};}
        SrList entry;entry.pin=list;
        if(auto recent=recentExecutions.find(list);recent!=recentExecutions.end()){
            // The exact list interface remained pinned from its actual Execute;
            // validate its device again before promoting any queue candidate.
            Microsoft::WRL::ComPtr<ID3D12Device> device;
            Microsoft::WRL::ComPtr<IUnknown> identity;
            if(FAILED(list->GetDevice(IID_PPV_ARGS(&device))) || FAILED(device.As(&identity)) ||
               !recent->second.device || recent->second.device.Get()!=identity.Get()){
                if(rejection)*rejection="queue_device_mismatch";
                ExplainLocked(bootstrap,list,"Executed list device identity mismatch");return {};
            }
            entry=recent->second;inherited=true;
            recentExecutions.erase(recent); // SR map now owns the same pins/history
        }
        it=srLists.emplace(list,std::move(entry)).first;
    }
    const auto& entry=it->second;
    const auto now=GetTickCount64();
    const bool sameOwner=owner && entry.queue.Get()==owner;
    if(!AcceptSrQueueHistory(entry.queue.Get()!=nullptr,entry.ambiguous,sameOwner,entry.tick,now)) {
        if(rejection)*rejection=entry.ambiguous?"queue_ambiguous":!entry.queue?"queue_unknown_list":
            now<entry.tick?"queue_clock_invalid":"queue_history_stale";
        ExplainLocked(bootstrap,list,entry.ambiguous?"SR list observed on multiple queues":"Waiting for recent SR queue observation");
        return {};
    }
    if(now-entry.tick>1500) {
        static uint64_t retained=0;
        if(++retained<=3 || retained%300==0)
            LOG_INFO("D18-F2 retained pinned SR queue: age-ms={} count={}; Execute+Signal still required",
                now-entry.tick,retained);
    }
    static uint64_t choices=0;
    if(++choices<=3 || choices%300==0)
        LOG_INFO("D18 SR queue selected: display={} execution={} list={} age-ms={}; completion still requires Execute+Signal",
            (void*)bootstrap,(void*)entry.queue.Get(),(void*)list,now-entry.tick);
    if(promoted)*promoted=inherited;
    if(rejection)*rejection="";
    return entry.queue;
}
inline Token Track(ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list) {
    if(!queue || !list) return {};
    if(queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
       list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT) return {};
    Microsoft::WRL::ComPtr<ID3D12Device> queueDevice, listDevice;
    Microsoft::WRL::ComPtr<IUnknown> queueIdentity, listIdentity;
    if(FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
       FAILED(list->GetDevice(IID_PPV_ARGS(&listDevice))) ||
       FAILED(queueDevice.As(&queueIdentity)) || FAILED(listDevice.As(&listIdentity)) ||
       queueIdentity.Get()!=listIdentity.Get()) return {};
    std::lock_guard lock(mutex);
    // The NR scratch set is currently singleton. Do not share it across concurrent queues/devices.
    if(owner && owner!=queue) {
        ExplainLocked(queue,list,"Swapchain queue differs from fixed NR queue"); return {};
    }
    if(!EnsureHookLocked(queue)) return {};
    size_t outstanding=0;
    for(auto& item:pending) outstanding+=item.second.size();
    if(outstanding>=64) return {}; // Unsubmitted/discarded recordings must never be retired by age.
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device)))) return {};
    auto token=std::make_shared<Ticket>();
    if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&token->fence)))) return {};
    if(!owner) {owner=queue;owner->AddRef();}
    pending[list].push_back(token);
    lastBlock.clear();
    return token;
}
// ResTrack owns hook teardown. Tickets and pins must outlive in-flight/discarded recordings.
inline void Unhook() {}
}
