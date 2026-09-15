#pragma once
#include <inputs/FG/Upscaler_Inputs_Dx11wDx12.h>
#include <with_dx12/with_dx12.h>
#include <wrl/client.h>
#include <atomic>
#include "NativeFgPresentPolicy.h"
#include <mutex>
#include <filesystem>
#include <cstdio>
#include <share.h>

namespace DlssNr::NativeFgDx11 {
inline std::atomic<unsigned long long> attempts{0}, submitted{0}, skipped{0};
inline std::atomic<long> failure{0};
inline std::atomic<bool> ownsQueue{false};
inline std::atomic<unsigned long long> presented{0}, presentCalls{0}, lastGeneratedTick{0};
// Present owns this lock while modifying FG state. The render thread only try-locks;
// it must never wait for Present while holding the game's immediate context.
inline std::mutex inputMutex;
inline std::atomic<int> enabledRequest{-1};
inline ID3D12CommandQueue* ownerQueue=nullptr;
inline Microsoft::WRL::ComPtr<ID3D12Fence> queueFence, runtimeFence;
inline UINT64 queueValue=0, runtimeValue=0, generation=0;
inline int recordedIndex=-1;
inline bool dispatched=false, sealed=false;
inline ULONGLONG pendingSince=0;
inline NativeFgPresentPolicy recovery;
inline std::atomic<bool> recovering{false};
inline uint32_t lastPresentHr=0,lastRuntimeStatus=0;
inline int lastRuntimeQuery=0;
inline ID3D12CommandQueue* lastObservedQueue=nullptr;

inline bool IsRoute() {
    const auto& s=State::Instance();
    return s.activeFgInput==FGInput::Upscaler && s.swapchainInteropApi==SwapchainInteropApi::Dx11wDx12 &&
           s.activeFgOutput==FGOutput::DLSSG;
}
// Independent of spdlog. Default off; lifetime budget and sparse steady-state output.
inline void Trace(const char* stage,long code=0, bool force=false) {
    if(Config::Instance()->DlssNrDiagnostics.value_or_default()==0)return;
    static FILE* file=[](){static char anchor;HMODULE module=nullptr;wchar_t path[32768]{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&anchor),&module)||!GetModuleFileNameW(module,path,32768))return static_cast<FILE*>(nullptr);
        return _wfsopen((std::filesystem::path(path).parent_path()/L"D18NativeFG.jsonl").c_str(),L"wb",_SH_DENYNO);}();
    static unsigned budget=512;static ULONGLONG last=0;
    const auto now=GetTickCount64();
    if(!file || !budget || (!force && generation>16 && now-last<5000))return;
    --budget;last=now;
    fprintf(file,"{\"event\":\"native_fg_sync\",\"stage\":\"%s\",\"tick\":%llu,\"thread\":%lu,\"generation\":%llu,\"index\":%d,\"code\":%ld,\"attempts\":%llu,\"submitted\":%llu,\"skipped\":%llu,\"queue\":\"%p\",\"queue_fence\":\"%p\",\"queue_value\":%llu,\"runtime_fence\":\"%p\",\"runtime_value\":%llu,\"present_calls\":%llu,\"presented\":%llu,\"present_hr\":%u,\"sl_query\":%d,\"sl_status\":%u,\"observed_queue\":\"%p\"}\n",
        stage,now,GetCurrentThreadId(),generation,recordedIndex,code,attempts.load(),submitted.load(),skipped.load(),
        ownerQueue,queueFence.Get(),queueValue,runtimeFence.Get(),runtimeValue,presentCalls.load(),presented.load(),lastPresentHr,lastRuntimeQuery,lastRuntimeStatus,lastObservedQueue);fflush(file);
}
inline bool RequestedEnabled() {
    const int request=enabledRequest.load();
    return request>=0 ? request!=0 : Config::Instance()->FGEnabled.value_or_default();
}
__declspec(noinline) inline void SetEnabled(bool on) { enabledRequest=on?1:0; }
inline void ApplyControl() {
    const auto request=enabledRequest.exchange(-1);
    if(request>=0){Config::Instance()->FGEnabled=request!=0;State::Instance().fgChanged=true;Trace("toggle",request,true);}
    if(failure){Config::Instance()->FGEnabled.set_volatile_value(false);}
}
inline void Fail(long code) { long expected=0; if(failure.compare_exchange_strong(expected,code))Trace("stopped",code,true); }
inline bool HasPendingCommands() { return recordedIndex>=0 && !dispatched; }
// Called after successful ExecuteCommandLists of the exact list containing our tags.
inline void TagsDispatched(int index, bool success) {
    if(recordedIndex!=index || dispatched)return;
    if(!success){Fail(-7);return;}
    dispatched=true;Trace("tags_dispatched");
}
// Called on the Present thread after the SL Present/GetState pair. No GPU Wait here.
__declspec(noinline) inline void Presented(ID3D12CommandQueue* queue, ID3D12Fence* consumedFence, UINT64 consumedValue,
                                          HRESULT result, int query, uint32_t status) {
    if(recordedIndex<0 || !dispatched || sealed || failure)return;
    const bool changed=lastPresentHr!=static_cast<uint32_t>(result)||lastRuntimeQuery!=query||lastRuntimeStatus!=status||lastObservedQueue!=queue;
    lastPresentHr=static_cast<uint32_t>(result);lastRuntimeQuery=query;lastRuntimeStatus=status;lastObservedQueue=queue;
    if(queue!=ownerQueue || !queueFence){Fail(-11);return;}
    const bool wasWaiting=recovery.waiting;
    const auto decision=recovery.Observe(GetTickCount64(),lastPresentHr,query,status);
    using Decision=NativeFgPresentPolicy::Decision;
    if(decision==Decision::PresentFailed){Fail(-10);return;}
    if(decision==Decision::RuntimeExpired){Fail(-12);return;}
    if(decision!=Decision::Ready){
        recovering=true;Trace("present_retry",int(decision),changed||!wasWaiting);return;
    }
    // A valid return only restarts the GPU completion deadline. Reuse still needs
    // the queue and SL consumption fences below, including after a long occlusion.
    if(wasWaiting){pendingSince=GetTickCount64();Trace("present_resumed",0,true);}
    runtimeFence=consumedFence;runtimeValue=consumedValue;
    if(FAILED(queue->Signal(queueFence.Get(),++queueValue))){Fail(-5);return;}
    sealed=true;Trace("consumption_fenced");
}
inline bool Reusable() {
    if(recordedIndex<0)return true;
    if(sealed){
        const auto q=queueFence->GetCompletedValue();
        const auto r=runtimeFence?runtimeFence->GetCompletedValue():runtimeValue;
        if(q==UINT64_MAX || r==UINT64_MAX){Fail(-8);return false;}
        if(q>=queueValue && r>=runtimeValue){
            if(recovering.exchange(false))Trace("recovery_complete",0,true);
            Trace("consumption_complete");recordedIndex=-1;sealed=false;dispatched=false;runtimeFence.Reset();return true;
        }
    }
    if(!recovery.waiting && GetTickCount64()-pendingSince>2000)Fail(-9);
    ++skipped;Trace("input_pending");return false;
}
inline void Submit(ID3D11Device* d,ID3D11DeviceContext* c,NVSDK_NGX_Parameter* parameters,const Dx11FgFrame& frame) {
    auto& state=State::Instance();
    if(!IsRoute() || !state.currentFG || state.isShuttingDown)return;
    std::unique_lock<std::mutex> lock(inputMutex,std::try_to_lock);
    if(!lock.owns_lock()){++skipped;return;}
    ApplyControl();
    if(failure || !Config::Instance()->FGEnabled.value_or_default())return;
    ++attempts;
    // Skipping FG does not change SR/NR output or enqueue a reverse DX11 GPU wait.
    if(!Reusable())return;
    UpscalerInputsDx11wDx12::Init(d,c,state.currentD3D12Device,state.currentCommandQueue);
    const auto q=WithDx12::GetD3D12CommandQueue();const auto dx12=WithDx12::GetD3D12Device();
    if(!q||!dx12||(ownerQueue&&ownerQueue!=q)){Fail(-1);return;}
    ownerQueue=q;
    if(!queueFence && FAILED(dx12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&queueFence)))){Fail(-3);return;}
    ownsQueue=true;
    state.currentFG->SetCommandQueue(FG_ResourceType::Depth,q);
    ++generation;Trace("prepare_begin");
    bool recorded=false;
    const bool ok=UpscalerInputsDx11wDx12::SubmitNative(parameters,frame,&recorded);
    if(recorded){recordedIndex=state.currentFG->GetIndex();pendingSince=GetTickCount64();dispatched=false;sealed=false;}
    if(ok)++submitted;
    Trace("prepare_end",ok?1:0);
}
}
