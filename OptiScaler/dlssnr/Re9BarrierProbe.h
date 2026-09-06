#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <mutex>
#include <detours/detours.h>
#include "Re9BarrierState.h"
namespace DlssNr::Re9Barrier {
using Legacy=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
using Enhanced=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList7*,UINT,const D3D12_BARRIER_GROUP*);
using Bundle=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12GraphicsCommandList*);
inline Legacy legacy=nullptr;inline Enhanced enhanced=nullptr;inline Bundle bundle=nullptr;
inline void* legacyEntry=nullptr;inline void* enhancedEntry=nullptr;inline void* bundleEntry=nullptr;
inline std::mutex hookMutex;
inline bool attempted=false,installed=false;
struct Interval;
inline thread_local Interval* active=nullptr;
struct Interval {
    struct Target {
        const char* name=nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        State state;
        unsigned enhancedCount=0,lastLayout=0;
        uint64_t lastAccess=0;
    };
    std::array<Target,4> targets;
    ID3D12GraphicsCommandList* list=nullptr;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> view7;
    bool armed=false;
    Interval(ID3D12GraphicsCommandList* cmd,NVSDK_NGX_Parameter* params,bool sample);
    ~Interval(){if(armed)active=nullptr;}
    void Report(unsigned id,uint64_t frame)const;
    // NGX API contract, not inferred from missing barriers or host-list history.
    bool SeedNgxContract(){
        if(!armed)return false;
        for(unsigned i=0;i<4;++i){auto& t=targets[i];
            if(!t.resource || !t.state.count)return false;
            const auto d=t.resource->GetDesc();
            if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.SampleDesc.Count!=1)return false;
            if(i==0 && !(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return false;
            for(unsigned j=0;j<i;++j)if(t.resource==targets[j].resource)return false;
        }
        for(unsigned i=0;i<4;++i){auto& t=targets[i];
            const uint32_t expected=i==0?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            for(unsigned j=0;j<t.state.count;++j){t.state.sub[j].known=true;t.state.sub[j].value=expected;}
        }
        return true;
    }
    bool MatchesNgxContract()const{
        if(!armed)return false;
        for(unsigned i=0;i<4;++i){uint32_t value=0;
            const uint32_t expected=i==0?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            if(!targets[i].resource || !targets[i].state.Uniform(value) || value!=expected)return false;
        }
        return true;
    }
    bool Matches(void* cmd)const{return cmd==list || cmd==view7.Get();}
    void RejectAll(const char* reason){for(auto& t:targets)if(t.resource)t.state.Reject(reason);}
};
inline void STDMETHODCALLTYPE HLegacy(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
    if(active && active->Matches(list) && barriers){
        for(UINT i=0;i<count;++i){const auto& b=barriers[i];
            for(auto& t:active->targets){if(!t.resource)continue;
                if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && b.Transition.pResource==t.resource.Get())
                    t.state.Transition(b.Transition.Subresource,(uint32_t)b.Transition.StateBefore,(uint32_t)b.Transition.StateAfter,(unsigned)b.Flags);
                else if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_UAV && (!b.UAV.pResource || b.UAV.pResource==t.resource.Get()))++t.state.uavs;
                else if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING &&
                    (!b.Aliasing.pResourceBefore || !b.Aliasing.pResourceAfter || b.Aliasing.pResourceBefore==t.resource.Get() || b.Aliasing.pResourceAfter==t.resource.Get()))
                    t.state.Reject("aliasing-observed");
            }
        }
    }
    legacy(list,count,barriers);
}
inline void STDMETHODCALLTYPE HEnhanced(ID3D12GraphicsCommandList7* list,UINT count,const D3D12_BARRIER_GROUP* groups){
    if(active && active->Matches(list) && groups){
        for(UINT g=0;g<count;++g)if(groups[g].Type==D3D12_BARRIER_TYPE_TEXTURE){
            for(UINT i=0;i<groups[g].NumBarriers;++i){const auto& b=groups[g].pTextureBarriers[i];
                for(auto& t:active->targets)if(t.resource && b.pResource==t.resource.Get()){
                    ++t.enhancedCount;t.lastLayout=(unsigned)b.LayoutAfter;t.lastAccess=(uint64_t)b.AccessAfter;
                    t.state.Reject("enhanced-layout-not-mapped");
                }
            }
        }
    }
    enhanced(list,count,groups);
}
inline void STDMETHODCALLTYPE HBundle(ID3D12GraphicsCommandList* list,ID3D12GraphicsCommandList* child){
    if(active && active->Matches(list))active->RejectAll("bundle-not-traversed");
    bundle(list,child);
}
inline bool Install(ID3D12GraphicsCommandList* list,ID3D12GraphicsCommandList7* view7){
    std::lock_guard lock(hookMutex);
    auto vt=*reinterpret_cast<void***>(list);
    void* e=view7?(*reinterpret_cast<void***>(view7))[80]:nullptr;
    if(view7){auto v7=*reinterpret_cast<void***>(view7);
        if(v7[26]!=vt[26] || v7[27]!=vt[27])return false; // Do not claim coverage across unhooked interface targets.
    }
    if(attempted)return installed && vt[26]==legacyEntry && vt[27]==bundleEntry && e==enhancedEntry;
    attempted=true;legacyEntry=vt[26];bundleEntry=vt[27];enhancedEntry=e;
    legacy=reinterpret_cast<Legacy>(legacyEntry);bundle=reinterpret_cast<Bundle>(bundleEntry);enhanced=reinterpret_cast<Enhanced>(e);
    LONG error=DetourTransactionBegin();
    if(error==NO_ERROR){
        error=DetourUpdateThread(GetCurrentThread());
        if(error==NO_ERROR)error=DetourAttach(reinterpret_cast<PVOID*>(&legacy),HLegacy);
        if(error==NO_ERROR)error=DetourAttach(reinterpret_cast<PVOID*>(&bundle),HBundle);
        if(error==NO_ERROR && enhanced)error=DetourAttach(reinterpret_cast<PVOID*>(&enhanced),HEnhanced);
        if(error!=NO_ERROR)DetourTransactionAbort();else error=DetourTransactionCommit();
    }
    installed=error==NO_ERROR;
    if(!installed){legacy=nullptr;bundle=nullptr;enhanced=nullptr;}
    LOG_INFO("RE9 RR barriers: installed={} commit={} enhanced-interface={}",installed,error,view7!=nullptr);
    return installed;
}
inline Interval::Interval(ID3D12GraphicsCommandList* cmd,NVSDK_NGX_Parameter* params,bool sample){
    if(!sample || !cmd || !params || active)return;
    list=cmd;
    const auto qi=cmd->QueryInterface(IID_PPV_ARGS(&view7));
    if(FAILED(qi) && qi!=E_NOINTERFACE)return;
    if(!Install(cmd,view7.Get()))return;
    const char* keys[]={"Output","Color","Depth","MotionVectors"};
    const char* aliases[]={"DLSSD.Output","DLSSD.Color","DLSSD.Depth","DLSSD.MotionVectors"};
    for(unsigned i=0;i<4;++i){auto& t=targets[i];t.name=keys[i];ID3D12Resource* r=nullptr;
        if(params->Get(keys[i],&r)!=NVSDK_NGX_Result_Success || !r){r=nullptr;
            if(params->Get(aliases[i],&r)!=NVSDK_NGX_Result_Success)r=nullptr;}
        if(!r){void* raw=nullptr;
            if(params->Get(keys[i],&raw)!=NVSDK_NGX_Result_Success || !raw){raw=nullptr;if(params->Get(aliases[i],&raw)!=NVSDK_NGX_Result_Success)raw=nullptr;}
            r=static_cast<ID3D12Resource*>(raw);}
        if(!r)continue;t.resource=r;
        const auto d=r->GetDesc();Microsoft::WRL::ComPtr<ID3D12Device> device;
        D3D12_FEATURE_DATA_FORMAT_INFO info{d.Format,0};
        if(d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && SUCCEEDED(r->GetDevice(IID_PPV_ARGS(&device))) &&
           SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,&info,sizeof(info)))){
            const uint64_t n=uint64_t(d.MipLevels)*d.DepthOrArraySize*info.PlaneCount;
            if(n>0 && n<=64)t.state.count=static_cast<unsigned>(n);
        }
    }
    active=this;armed=true;
}
inline void Interval::Report(unsigned id,uint64_t frame)const{
    if(!armed){LOG_INFO("RE9 RR states: id={} frame={} coverage=unavailable NR=disabled",id,frame);return;}
    for(const auto& t:targets){uint32_t value=0;const bool uniform=t.state.Uniform(value);
        LOG_INFO("RE9 RR states: id={} frame={} resource={} present={} scope=native-call-only subresources={} known-mask=0x{:X} uniform={} observed-state=0x{:X} reason={} transitions={} uavs={} enhanced={} layout={} access=0x{:X} gpu-completion=UNOBSERVED",
            id,frame,t.name,t.resource!=nullptr,t.state.count,t.state.KnownMask(),uniform,uniform?value:0,t.state.Reason(),
            t.state.transitions,t.state.uavs,t.enhancedCount,t.lastLayout,t.lastAccess);
    }
}
inline void Unhook(){
    std::lock_guard lock(hookMutex);if(!installed)return;
    LONG error=DetourTransactionBegin();if(error!=NO_ERROR)return;
    error=DetourUpdateThread(GetCurrentThread());
    if(error==NO_ERROR)error=DetourDetach(reinterpret_cast<PVOID*>(&legacy),HLegacy);
    if(error==NO_ERROR)error=DetourDetach(reinterpret_cast<PVOID*>(&bundle),HBundle);
    if(error==NO_ERROR && enhanced)error=DetourDetach(reinterpret_cast<PVOID*>(&enhanced),HEnhanced);
    if(error!=NO_ERROR)DetourTransactionAbort();else error=DetourTransactionCommit();
    if(error==NO_ERROR){installed=false;legacy=nullptr;bundle=nullptr;enhanced=nullptr;}
}
}
