#pragma once
// Native compute-state support: exact PSO/root shader-use proof and list invalidation.
#include <d3d12shader.h>
#include <d3dx/d3dx12.h>
#include <wrl/client.h>
#include "NrShaderUsage.h"

namespace NrNativeStateSupport {
using Microsoft::WRL::ComPtr;
inline bool Enabled() { return Config::Instance()->NgxOnlyMode.value_or_default() &&
    (_stricmp(State::Instance().gameExe.c_str(), "OnimushaWotS.exe") == 0); }
struct Shader {
    ComPtr<ID3D12PipelineState> keep;
    std::vector<uint8_t> bytes;
    ID3D12RootSignature* root = nullptr;
    std::string route;
    bool reported = false;
    bool inspected = false;
    NrState::ShaderUsage usage;
};
inline std::mutex mutex;
inline std::unordered_map<ID3D12PipelineState*, Shader> shaders;
inline size_t retainedBytes=0;
inline void (*onReset)(ID3D12GraphicsCommandList*) = nullptr;
inline std::atomic<uint64_t> captures{0}, rejected{0}, streamErrors{0};
inline bool NoSamplers(ID3D12PipelineState* pso,ID3D12RootSignature* root) {
    if(!Enabled() || !pso || !root) return false;
    std::lock_guard lock(mutex);
    auto it=shaders.find(pso);
    if(it==shaders.end() || it->second.root!=root) return false;
    auto& record=it->second;
    if(!record.inspected) {record.usage=NrState::InspectDxil(record.bytes.data(),record.bytes.size());record.inspected=true;}
    return record.usage.known && record.usage.samplers==0;
}
inline void Capture(void* object, const D3D12_SHADER_BYTECODE& cs, ID3D12RootSignature* root, const char* route) {
    if (!Enabled() || !object || !cs.pShaderBytecode || !cs.BytecodeLength) return;
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(static_cast<IUnknown*>(object)->QueryInterface(IID_PPV_ARGS(&pso)))) return;
    std::lock_guard lock(mutex);
    if(shaders.contains(pso.Get())) return;
    if(shaders.size()>=512 || cs.BytecodeLength>8*1024*1024 || retainedBytes+cs.BytecodeLength>64*1024*1024) {
        ++rejected; return;
    }
    Shader record;
    record.keep=pso; record.root=root; record.route=route;
    auto data=static_cast<const uint8_t*>(cs.pShaderBytecode);
    record.bytes.assign(data,data+cs.BytecodeLength);
    retainedBytes+=record.bytes.size(); ++captures;
    shaders.emplace(pso.Get(),std::move(record));
}
struct Stream : ID3DX12PipelineParserCallbacks {
    D3D12_SHADER_BYTECODE cs{}; ID3D12RootSignature* root=nullptr;
    void CSCb(const D3D12_SHADER_BYTECODE& value) override {cs=value;}
    void RootSignatureCb(ID3D12RootSignature* value) override {root=value;}
};
inline void CaptureStream(void* object,const D3D12_PIPELINE_STATE_STREAM_DESC* desc,const char* route) {
    if(!desc) return;
    Stream parsed;
    if(FAILED(D3DX12ParsePipelineStream(*desc,&parsed))) {++streamErrors; return;}
    Capture(object,parsed.cs,parsed.root,route);
}
using Compute=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_COMPUTE_PIPELINE_STATE_DESC*,REFIID,void**);
using Pipeline=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device2*,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
using CreateLibrary=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device1*,const void*,SIZE_T,REFIID,void**);
using LoadCompute=HRESULT(STDMETHODCALLTYPE*)(ID3D12PipelineLibrary*,LPCWSTR,const D3D12_COMPUTE_PIPELINE_STATE_DESC*,REFIID,void**);
using LoadPipeline=HRESULT(STDMETHODCALLTYPE*)(ID3D12PipelineLibrary1*,LPCWSTR,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
using Reset=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using Clear=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12PipelineState*);
inline Clear clear=nullptr;
inline Compute compute=nullptr; inline Pipeline pipeline=nullptr; inline CreateLibrary createLibrary=nullptr;
inline LoadCompute loadCompute=nullptr; inline LoadPipeline loadPipeline=nullptr; inline Reset reset=nullptr;
inline std::mutex installMutex;
inline void* resetEntry=nullptr;
inline void* clearEntry=nullptr;
inline bool SupportsList(ID3D12GraphicsCommandList* list) {
    if(!list) return false;
    std::lock_guard lock(installMutex);
    auto vt=*reinterpret_cast<void***>(list);
    return reset && clear && vt[10]==resetEntry && vt[11]==clearEntry;
}
template<class T> inline bool Attach(T& original,void* address,T hook) {
    if(original || !address) return original!=nullptr;
    original=reinterpret_cast<T>(address);
    LONG error=DetourTransactionBegin();
    if(error!=NO_ERROR) {original=nullptr; return false;}
    error=DetourUpdateThread(GetCurrentThread());
    if(error==NO_ERROR) error=DetourAttach(reinterpret_cast<PVOID*>(&original),reinterpret_cast<PVOID>(hook));
    if(error!=NO_ERROR) DetourTransactionAbort(); else error=DetourTransactionCommit();
    if(error!=NO_ERROR) original=nullptr;
    return error==NO_ERROR;
}
inline HRESULT STDMETHODCALLTYPE HCompute(ID3D12Device* d,const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,REFIID id,void** out) {
    auto result=compute(d,desc,id,out);
    if(SUCCEEDED(result) && out && desc) Capture(*out,desc->CS,desc->pRootSignature,"compute/cache-desc");
    return result;
}
inline HRESULT STDMETHODCALLTYPE HPipeline(ID3D12Device2* d,const D3D12_PIPELINE_STATE_STREAM_DESC* desc,REFIID id,void** out) {
    auto result=pipeline(d,desc,id,out);
    if(SUCCEEDED(result) && out) CaptureStream(*out,desc,"device-stream");
    return result;
}
inline HRESULT STDMETHODCALLTYPE HLoadCompute(ID3D12PipelineLibrary* d,LPCWSTR name,const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,REFIID id,void** out) {
    auto result=loadCompute(d,name,desc,id,out);
    if(SUCCEEDED(result) && out && desc) Capture(*out,desc->CS,desc->pRootSignature,"library-compute");
    return result;
}
inline HRESULT STDMETHODCALLTYPE HLoadPipeline(ID3D12PipelineLibrary1* d,LPCWSTR name,const D3D12_PIPELINE_STATE_STREAM_DESC* desc,REFIID id,void** out) {
    auto result=loadPipeline(d,name,desc,id,out);
    if(SUCCEEDED(result) && out) CaptureStream(*out,desc,"library-stream");
    return result;
}
inline HRESULT STDMETHODCALLTYPE HLibrary(ID3D12Device1* d,const void* blob,SIZE_T size,REFIID id,void** out) {
    auto result=createLibrary(d,blob,size,id,out);
    if(SUCCEEDED(result) && out && *out && Enabled()) {
        ComPtr<ID3D12PipelineLibrary> library; ComPtr<ID3D12PipelineLibrary1> library1;
        if(SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&library)))) {
            std::lock_guard lock(installMutex);
            auto vt=*reinterpret_cast<void***>(library.Get());
            bool a=Attach(loadCompute,vt[10],HLoadCompute), b=false;
            if(SUCCEEDED(library.As(&library1))) b=Attach(loadPipeline,(*reinterpret_cast<void***>(library1.Get()))[13],HLoadPipeline);
            LOG_INFO("D22 combined library hooks compute={} stream={}",a,b);
        }
    }
    return result;
}
inline HRESULT STDMETHODCALLTYPE HReset(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* pso) {
    auto result=reset(list,allocator,pso);
    if(SUCCEEDED(result) && onReset) onReset(list);
    return result;
}
inline void STDMETHODCALLTYPE HClear(ID3D12GraphicsCommandList* list,ID3D12PipelineState* pso) {
    clear(list,pso);
    if(onReset) onReset(list);
}
inline void InstallList(ID3D12GraphicsCommandList* list) {
    if(!Enabled() || !list) return;
    static bool attempted=false;
    std::lock_guard lock(installMutex);
    if(attempted) return; attempted=true;
    resetEntry=(*reinterpret_cast<void***>(list))[10]; clearEntry=(*reinterpret_cast<void***>(list))[11];
    LOG_INFO("D22 combined reset hook={}",Attach(reset,resetEntry,HReset));
    LOG_INFO("D22 combined clear-state hook={}",Attach(clear,clearEntry,HClear));
}
inline void InstallDevice(ID3D12Device* device) {
    if(!Enabled() || !device) return;
    std::lock_guard lock(installMutex);
    ComPtr<ID3D12Device1> d1; ComPtr<ID3D12Device2> d2;
    bool a=Attach(compute,(*reinterpret_cast<void***>(device))[11],HCompute), b=false,c=false;
    if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d2)))) b=Attach(pipeline,(*reinterpret_cast<void***>(d2.Get()))[47],HPipeline);
    if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d1)))) c=Attach(createLibrary,(*reinterpret_cast<void***>(d1.Get()))[44],HLibrary);
    LOG_INFO("D22 combined device hooks compute={} stream={} library={}; other implementations remain unknown",a,b,c);
}
inline void Unhook() {
    std::lock_guard lock(installMutex);
    if(!compute && !pipeline && !createLibrary && !loadCompute && !loadPipeline && !reset && !clear) return;
    LONG error=DetourTransactionBegin();
    if(error!=NO_ERROR) return;
    error=DetourUpdateThread(GetCurrentThread());
    auto detach=[&](auto& original,auto hook) {
        if(original && error==NO_ERROR)
            error=DetourDetach(reinterpret_cast<PVOID*>(&original),reinterpret_cast<PVOID>(hook));
    };
    detach(compute,HCompute); detach(pipeline,HPipeline); detach(createLibrary,HLibrary);
    detach(loadCompute,HLoadCompute); detach(loadPipeline,HLoadPipeline); detach(reset,HReset);
    detach(clear,HClear);
    if(error!=NO_ERROR) DetourTransactionAbort(); else error=DetourTransactionCommit();
    if(error==NO_ERROR) {compute=nullptr;pipeline=nullptr;createLibrary=nullptr;loadCompute=nullptr;loadPipeline=nullptr;reset=nullptr;clear=nullptr;}
    // Retained evidence/COM objects live until process exit; no racing resource release here.
}
}
