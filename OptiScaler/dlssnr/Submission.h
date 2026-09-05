#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <detours/detours.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <string>
#include "SrQueuePolicy.h"

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
using Execute = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
inline Execute original = nullptr;
inline void* installed = nullptr;
inline ID3D12CommandQueue* owner = nullptr;
inline std::mutex mutex;
inline std::unordered_map<ID3D12CommandList*,std::vector<Token>> pending;
// Bounded observation only. Raw addresses are never dereferenced or used to authorize NR.
struct Seen {uintptr_t queue=0; ULONGLONG tick=0;};
inline std::unordered_map<ID3D12CommandList*,Seen> observed;
// Watch only lists presented at the native SR seam. COM pins prevent address reuse.
// History selects a candidate; only the later actual Execute + Signal completes its ticket.
struct SrList {
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pin;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    ULONGLONG tick=0;
    bool ambiguous=false;
};
inline std::unordered_map<ID3D12CommandList*,SrList> srLists;
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
inline void STDMETHODCALLTYPE Hook(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    std::vector<Token> batch;
    {
        std::lock_guard lock(mutex);
        ++executeCalls;if(queue==owner) ++ownerCalls;
        for(UINT i=0;i<count;++i) {
            if(auto sr=srLists.find(lists[i]);sr!=srLists.end()) {
                auto& entry=sr->second;
                if(entry.queue && entry.queue.Get()!=queue) entry.ambiguous=true;
                if(!entry.queue) entry.queue=queue;
                entry.tick=GetTickCount64();
            }
            if(!observed.contains(lists[i]) && observed.size()>=2048) observed.clear();
            observed[lists[i]]={reinterpret_cast<uintptr_t>(queue),GetTickCount64()};
        }
        for (UINT i=0;queue==owner && i<count;++i) {
            auto it=pending.find(lists[i]);
            if(it!=pending.end()) { batch.insert(batch.end(),it->second.begin(),it->second.end()); pending.erase(it); }
        }
    }
    original(queue,count,lists);
    // Signal only AFTER the actual batch submission, never during CPU command recording.
    for(auto& ticket:batch)
        if(SUCCEEDED(queue->Signal(ticket->fence.Get(),1))) ticket->submitted.store(true);
}
// Caller holds mutex. Installing observation must not assign NR scratch ownership.
inline bool EnsureHookLocked(ID3D12CommandQueue* queue) {
    if(!queue) return false;
    auto address=(*reinterpret_cast<void***>(queue))[10];
    if(installed) return address==installed;
    original=reinterpret_cast<Execute>(address);
    if(DetourTransactionBegin()!=NO_ERROR) {original=nullptr;return false;}
    if(DetourUpdateThread(GetCurrentThread())!=NO_ERROR ||
       DetourAttach(reinterpret_cast<PVOID*>(&original),Hook)!=NO_ERROR) {
        DetourTransactionAbort();original=nullptr;return false;
    }
    if(DetourTransactionCommit()!=NO_ERROR) {original=nullptr;return false;}
    installed=address;
    return true;
}
inline Microsoft::WRL::ComPtr<ID3D12CommandQueue> ResolveSrQueue(
        ID3D12CommandQueue* bootstrap,ID3D12GraphicsCommandList* list) {
    if(!list || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT) return {};
    std::lock_guard lock(mutex);
    if(!installed && !EnsureHookLocked(bootstrap)) {
        ExplainLocked(bootstrap,list,"Cannot install SR queue observer");return {};
    }
    auto it=srLists.find(list);
    if(it==srLists.end()) {
        if(srLists.size()>=256) {ExplainLocked(bootstrap,list,"SR list observation limit");return {};}
        SrList entry;entry.pin=list;
        it=srLists.emplace(list,std::move(entry)).first;
    }
    const auto& entry=it->second;
    const auto now=GetTickCount64();
    const bool sameOwner=owner && entry.queue.Get()==owner;
    if(!AcceptSrQueueHistory(entry.queue.Get()!=nullptr,entry.ambiguous,sameOwner,entry.tick,now)) {
        ExplainLocked(bootstrap,list,entry.ambiguous?"SR list observed on multiple queues":"Waiting for recent SR queue observation");
        return {};
    }
    // F2: a rarely reused list can be older than 1500 ms at 30 FPS while still
    // belonging to the same pinned queue. Do not drop an otherwise valid NR frame.
    if(now-entry.tick>1500) {
        static uint64_t agedAccepted=0;
        ++agedAccepted;
        if(agedAccepted<=3 || agedAccepted%300==0) {
            LOG_DEBUG("D18 retained pinned SR queue: age-ms={} list={} queue={} owner={} count={}; Execute+Signal still required",
                now-entry.tick,(void*)list,(void*)entry.queue.Get(),(void*)owner,agedAccepted);
        }
    }
    static uint64_t choices=0;
    if(++choices<=3 || choices%300==0)
        LOG_INFO("D18 SR queue selected: display={} execution={} list={} age-ms={}; completion still requires Execute+Signal",
            (void*)bootstrap,(void*)entry.queue.Get(),(void*)list,now-entry.tick);
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
inline void Unhook() {
    std::lock_guard lock(mutex);
    if (!installed) return;
    if (DetourTransactionBegin()!=NO_ERROR) return;
    if (DetourUpdateThread(GetCurrentThread())!=NO_ERROR ||
        DetourDetach(reinterpret_cast<PVOID*>(&original),Hook)!=NO_ERROR) {
        DetourTransactionAbort(); return;
    }
    if (DetourTransactionCommit()==NO_ERROR) installed=nullptr;
    // Keep the queue reference while there may be discarded/in-flight recordings.
}
}
