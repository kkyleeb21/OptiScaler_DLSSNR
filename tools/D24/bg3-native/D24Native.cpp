// Single-owner native DX11 adapter with shared, bounded patch-site validation.
#include <windows.h>
#include <share.h>
#include <d3dcompiler.h>
#pragma warning(push)
#pragma warning(disable:4324)
#include <shaders/dlssnr/DlssNr_Common.h>
#pragma warning(pop)
#include "compose_shader.h"
#include <dlssnr/NativeControlAbi.h>
#include <dlssnr/NativeSampler.h>
#include <d3d11_1.h>
#include "dx11_completion.h"
#include <array>
#include "../runtime-guard/check.h"
#include <mutex>
#include <atomic>
#include "nr_jitter_policy.h"
#include <filesystem>
#include <wrl/client.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include "dx11_probe_parameters.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"d3dcompiler.lib")
static bool managed=false,modelDirty=false;
static DlssNrNative::Settings control;
static DlssNrNative::Status liveStatus;
static FILE* logFile=nullptr;
#include "dx11_log.h"
static void event(const char* name, long long value) { if(logFile&&allowNativeEvent(name,GetTickCount64())){logPrint(logFile,"{\"event\":\"%s\",\"value\":%lld}\n",name,value);fflush(logFile);} }
static unsigned char* imageBase=nullptr;
static void* nativeBackend=nullptr;
static void** nativeVtable=nullptr;
static std::vector<void*> vaResources;
static std::vector<void*> residentResources;
static void track(void* resource){if(resource&&std::find(residentResources.begin(),residentResources.end(),resource)==residentResources.end())residentResources.push_back(resource);}
static int trackedBuffer(void* backend,const unsigned* desc,void** out,const char* name,int flags){
    int status=reinterpret_cast<int(*)(void*,const unsigned*,void**,const char*,int)>(nativeVtable[0x70/8])(backend,desc,out,name,flags);
    if(!status&&desc[6]==1)track(*out);return status;
}
static int trackedTexture(void* backend,const unsigned* desc,void** out,const char* name,int flags){
    int status=reinterpret_cast<int(*)(void*,const unsigned*,void**,const char*,int)>(nativeVtable[0x78/8])(backend,desc,out,name,flags);
    if(!status)track(*out);return status;
}
static int trackedRelease(void* backend,void* resource){
    int status=reinterpret_cast<int(*)(void*,void*)>(nativeVtable[0xa0/8])(backend,resource);
    if(!status){residentResources.erase(std::remove(residentResources.begin(),residentResources.end(),resource),residentResources.end());vaResources.erase(std::remove(vaResources.begin(),vaResources.end(),resource),vaResources.end());}
    return status;
}
static unsigned registeredDispatches=0;
alignas(16) static unsigned char pendingKernel[0x140]{};
static int appendHandle(unsigned char* state,void* handle,bool write) {
    auto count=reinterpret_cast<unsigned*>(state+(write?0x34:0x30));
    auto total=*reinterpret_cast<unsigned*>(state+0x30)+*reinterpret_cast<unsigned*>(state+0x34);
    auto list=reinterpret_cast<void**>(state+(write?0xc0:0x40));
    for(unsigned i=0;i<*count;++i)if(list[i]==handle)return 0;
    if(*count>=(write?16u:16u)||total>=24)return -1;
    list[(*count)++]=handle;return 0;
}
static int pureBufferVA(void* backend,void* resource,uint64_t* output) {
    void* device=*reinterpret_cast<void**>(static_cast<unsigned char*>(backend)+0x460);
    void* handle=nullptr;
    int status=reinterpret_cast<int(*)(void*,void*,void**)>(imageBase+0x1ad0)(device,resource,&handle);
    if(!status)status=reinterpret_cast<int(*)(void*,void*,uint64_t*)>(imageBase+0x38a0)(device,handle,output);
    if(!status&&resource&&std::find(vaResources.begin(),vaResources.end(),resource)==vaResources.end())vaResources.push_back(resource);
    event("adapter_buffer_va",status);return status;
}
static int registeredBegin(void* backend,void* kernel) {
    int status=reinterpret_cast<int(*)(void*,void*)>(nativeVtable[0xd8/8])(backend,kernel);
    if(status)return status;
    for(void* resource:residentResources) {
        status=reinterpret_cast<int(*)(void*,void*)>(nativeVtable[0xc8/8])(backend,resource);if(status)return status;
        status=reinterpret_cast<int(*)(void*,void*)>(nativeVtable[0xd0/8])(backend,resource);if(status)return status;
    }
    auto state=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(backend)+0x140);
    for(unsigned write=0;write<2;++write){auto count=*reinterpret_cast<unsigned*>(pendingKernel+(write?0x34:0x30));auto list=reinterpret_cast<void**>(pendingKernel+(write?0xc0:0x40));
        for(unsigned i=0;i<count;++i)if(appendHandle(state,list[i],write!=0))return -1;}
    ++registeredDispatches;
    return 0;
}
static int clearBuffer(void*,ID3D11DeviceContext* context,ID3D11Resource* resource,unsigned value) {
    ID3D11Buffer* buffer=nullptr;HRESULT hr=resource->QueryInterface(__uuidof(ID3D11Buffer),reinterpret_cast<void**>(&buffer));
    if(FAILED(hr))return -5;
    D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
    if(!(desc.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)||desc.ByteWidth%4){buffer->Release();return -5;}
    ID3D11Device* device=nullptr;context->GetDevice(&device);
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_TYPELESS;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements=desc.ByteWidth/4;ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
    ID3D11UnorderedAccessView* view=nullptr;hr=device->CreateUnorderedAccessView(buffer,&ud,&view);
    if(SUCCEEDED(hr)){unsigned values[4]={value,value,value,value};context->ClearUnorderedAccessViewUint(view,values);view->Release();}
    device->Release();buffer->Release();event("adapter_clear_buffer",hr);return SUCCEEDED(hr)?0:-1;
}

using Microsoft::WRL::ComPtr;
#include "dx11_perf.h"
struct Session {
    NativePerf perf;
    std::mutex mutex;
    bool enabled=false,failed=false,keyDown=false,constructed=false;
    int allocationFailure=0;
    void* owner=nullptr;HMODULE module=nullptr;
    alignas(8) unsigned char common[0xa8]{};
    void* handle=nullptr;void* adapted[0x300/8]{};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> isolated;
    ComPtr<ID3D11Texture2D> input,output,ownedDepth,ownedMotion,filtered;
    ComPtr<ID3D11Texture2D> nrMotion;
    ComPtr<ID3D11ComputeShader> convertJitter;ComPtr<ID3D11Buffer> jitterConstants;
    NrJitterHistory jitterHistory;NrJitterPlan jitterPlan;NrJitterSample jitterSample;unsigned jitterRecords=0;
    unsigned jitterMode=0,jitterLastLogged=UINT32_MAX;DlssNrNative::JitterStatus jitterStatus{};
    ComPtr<ID3D11Texture2D> exposureStaging;
    float exposure=0,exposurePre=1,pendingPre=1;bool exposurePending=false,pendingExposurePair=false;
    bool exposureAllocationFailed=false;
    unsigned exposureAllocationAttempts=0;
    ComPtr<ID3D11Query> query;
    Dx11Completion completion;
    struct SrvEntry { ID3D11Resource* resource=nullptr; bool explicitDesc=false; D3D11_SHADER_RESOURCE_VIEW_DESC desc{}; ComPtr<ID3D11ShaderResourceView> view; };
    struct UavEntry { ID3D11Resource* resource=nullptr; bool explicitDesc=false; D3D11_UNORDERED_ACCESS_VIEW_DESC desc{}; ComPtr<ID3D11UnorderedAccessView> view; };
    std::array<SrvEntry,8> srvCache{}; std::array<UavEntry,8> uavCache{};
    unsigned srvNext=0,uavNext=0;
    bool ownViewResource(ID3D11Resource* r)const {
        return r&&(r==input.Get()||r==output.Get()||r==ownedDepth.Get()||r==ownedMotion.Get()||r==nrMotion.Get()||r==filtered.Get()||r==keep.Get());
    }
    void clearViews(){for(auto& v:srvCache)v={};for(auto& v:uavCache)v={};srvNext=uavNext=0;}

    ComPtr<ID3D11ComputeShader> convert,compose;
    ComPtr<ID3D11Buffer> constants; ComPtr<ID3D11SamplerState> sampler; ComPtr<ID3D11Texture2D> keep; unsigned mode=2;
    std::vector<ComPtr<ID3D11Resource>> inflight;
    D3D11_TEXTURE2D_DESC desc{};
    ProbeParameters parameters;
    unsigned frames=0,epoch=0;uint64_t calls=0;
};
static HRESULT countedSrv(Session& s,ID3D11Resource* resource,const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,ID3D11ShaderResourceView** view){
 const bool owned=s.ownViewResource(resource);
 if(owned)for(auto& entry:s.srvCache)if(entry.view&&entry.resource==resource&&entry.explicitDesc==(desc!=nullptr)&&(!desc||memcmp(&entry.desc,desc,sizeof(*desc))==0))return entry.view.CopyTo(view);
 if(s.perf.enabled)++s.perf.srv;
 auto hr=s.device->CreateShaderResourceView(resource,desc,view);
 if(SUCCEEDED(hr)&&owned){auto& entry=s.srvCache[s.srvNext++%s.srvCache.size()];entry={};entry.resource=resource;entry.explicitDesc=desc!=nullptr;if(desc)entry.desc=*desc;entry.view=*view;}
 return hr;
}
static HRESULT countedUav(Session& s,ID3D11Resource* resource,const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc,ID3D11UnorderedAccessView** view){
 const bool owned=s.ownViewResource(resource);
 if(owned)for(auto& entry:s.uavCache)if(entry.view&&entry.resource==resource&&entry.explicitDesc==(desc!=nullptr)&&(!desc||memcmp(&entry.desc,desc,sizeof(*desc))==0))return entry.view.CopyTo(view);
 if(s.perf.enabled)++s.perf.uav;
 auto hr=s.device->CreateUnorderedAccessView(resource,desc,view);
 if(SUCCEEDED(hr)&&owned){auto& entry=s.uavCache[s.uavNext++%s.uavCache.size()];entry={};entry.resource=resource;entry.explicitDesc=desc!=nullptr;if(desc)entry.desc=*desc;entry.view=*view;}
 return hr;
}
static Session& session(){static Session* value=new Session;return *value;}
static std::filesystem::path directory(){
    HMODULE self=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&directory),&self);
    wchar_t path[32768]{};GetModuleFileNameW(self,path,32768);return std::filesystem::path(path).parent_path();
}
// Same on-disk validator as the installer helper; no Runtime is executed here.
extern "C" __declspec(dllexport) int D24CheckRuntime(const wchar_t* path,unsigned* failedOffset){
    if(!path)return 0;
    const auto check=D18RuntimeGuard::CheckFile(path);
    if(failedOffset)*failedOffset=check.offset;
    return check.accepted?1:0;
}
static bool runtimeLayout(const std::filesystem::path& path){
    const auto check=D18RuntimeGuard::CheckFile(path);
    if(logFile && allowNativeEvent("runtime_layout",GetTickCount64())){
        logPrint(logFile,"{\"event\":\"runtime_layout\",\"rule\":\"%s\",\"accepted\":%s,\"reason\":\"%s\",\"region\":\"%s\",\"offset\":%u}\n",D18RuntimeGuard::Rule,check.accepted?"true":"false",check.reason,check.region,check.offset);
        fflush(logFile);
    }
    return check.accepted;
}
static bool completed(Session& s,unsigned phase=2){
    const auto started=s.perf.enabled?perfTick():0;
    BOOL done=FALSE;HRESULT hr;
    if(s.completion.available()){
        unsigned long long polls=0;hr=s.completion.wait(polls);done=hr==S_OK;
        if(s.perf.enabled)s.perf.polls+=polls;
    }else{
        s.context->End(s.query.Get());s.context->Flush();const auto end=GetTickCount64()+2000;
        do{hr=s.context->GetData(s.query.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(s.perf.enabled)++s.perf.polls;if(hr!=S_FALSE)break;Sleep(0);}while(GetTickCount64()<end);
    }
    if(s.perf.enabled){++s.perf.waits[phase];s.perf.waitUs[phase]+=perfUs(started);}
    if(hr!=S_OK||!done){event("completion_failed",hr);s.failed=true;s.enabled=false;return false;}return true;
}
struct StateScope{
    ID3D11DeviceContext1* ctx;ComPtr<ID3DDeviceContextState> previous;
    explicit StateScope(Session& s):ctx(s.context.Get()){ctx->SwapDeviceContextState(s.isolated.Get(),&previous);}
    ~StateScope(){ctx->SwapDeviceContextState(previous.Get(),nullptr);}
};
static bool releaseFeature(Session& s){
    if(!s.handle)return true;
    if(!completed(s))return false;
    int code=reinterpret_cast<int(*)(void*,void*)>(imageBase+0x1aeb0)(s.common,s.handle);event("release",static_cast<unsigned>(code));
    if(code!=1){s.failed=true;return false;}
    s.clearViews();
    s.handle=nullptr;s.ownedDepth.Reset();s.ownedMotion.Reset();s.nrMotion.Reset();s.jitterHistory.clear();s.input.Reset();s.output.Reset();s.keep.Reset();s.parameters.Reset();residentResources.clear();vaResources.clear();s.frames=0;
    return true;
}
static bool initialize(Session& s,ID3D11DeviceContext* ctx){
    if(s.module)return true;
    auto root=directory();auto runtime=root/L"D24Runtime.dll";
    if(!runtimeLayout(runtime)){event("runtime_layout_rejected",1);return false;}
    // Do not share a process image that another NR implementation already owns.
    if(GetModuleHandleW(L"D24Runtime.dll")){event("runtime_already_loaded",1);return false;}
    ctx->GetDevice(&s.device);if(FAILED(ctx->QueryInterface(IID_PPV_ARGS(&s.context))))return false;
    ComPtr<ID3D11Device1> d1;if(FAILED(s.device.As(&d1)))return false;
    auto fl=s.device->GetFeatureLevel();
    if(FAILED(d1->CreateDeviceContextState(0,&fl,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&s.isolated)))return false;
    D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};if(FAILED(s.device->CreateQuery(&q,&s.query)))return false;
    event("dx11_event_completion",s.completion.initialize(s.device.Get(),s.context.Get())?1:0);
    s.module=LoadLibraryExW(runtime.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!s.module)return false;
    imageBase=reinterpret_cast<unsigned char*>(s.module);nativeBackend=imageBase+0x1153550;
    nativeVtable=*reinterpret_cast<void***>(nativeBackend);
    const unsigned char expected[]={0x4c,0x8d,0x0d,0x8d,0xf2,0x08,0x00};
    if(nativeVtable!=reinterpret_cast<void**>(imageBase+0xb55f8)||memcmp(imageBase+0x20f2c,expected,7))return false;
    DWORD old=0;if(!VirtualProtect(imageBase+0x20f2c,7,PAGE_EXECUTE_READWRITE,&old))return false;
    const unsigned char jump[]={0xe9,0x24,0,0,0,0x90,0x90};memcpy(imageBase+0x20f2c,jump,7);
    DWORD ignored=0;VirtualProtect(imageBase+0x20f2c,7,old,&ignored);FlushInstructionCache(GetCurrentProcess(),imageBase+0x20f2c,7);
    memcpy(s.adapted,nativeVtable,sizeof(s.adapted));
    s.adapted[0x70/8]=reinterpret_cast<void*>(&trackedBuffer);s.adapted[0x78/8]=reinterpret_cast<void*>(&trackedTexture);
    s.adapted[0xa0/8]=reinterpret_cast<void*>(&trackedRelease);s.adapted[0xb8/8]=reinterpret_cast<void*>(&pureBufferVA);
    s.adapted[0xd8/8]=reinterpret_cast<void*>(&registeredBegin);s.adapted[0x1b0/8]=reinterpret_cast<void*>(&clearBuffer);
    *reinterpret_cast<void***>(nativeBackend)=s.adapted;
    reinterpret_cast<void*(*)(void*)>(imageBase+0x16860)(s.common);s.constructed=true;
    int code=reinterpret_cast<int(*)(void*,void*,const wchar_t*,void*,int)>(imageBase+0x19e70)(s.common,s.device.Get(),root.c_str(),nativeBackend,0);
    event("native_init",static_cast<unsigned>(code));return code==1;
}
static bool makeFeature(Session& s,const D3D11_TEXTURE2D_DESC& original,bool modelRequired=true){
    if(s.input&&(!modelRequired||s.handle)&&s.desc.Width==original.Width&&s.desc.Height==original.Height&&s.desc.Format==original.Format)return true;
    if(!releaseFeature(s))return false;
    s.desc=original;D3D11_TEXTURE2D_DESC td=original;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.MipLevels=1;td.ArraySize=1;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;td.MiscFlags=0;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> input,output,keep,filtered;
    const auto allocate=[&](ComPtr<ID3D11Texture2D>& target,const char* role){
        HRESULT hr=s.device->CreateTexture2D(&td,nullptr,&target);
        if(SUCCEEDED(hr)&&target)return true;
        if(SUCCEEDED(hr))hr=E_POINTER;
        const auto removed=s.device->GetDeviceRemovedReason();
        s.allocationFailure=FAILED(removed)?-27:-26;
        if(logFile){logPrint(logFile,"{\"event\":\"allocation_failed\",\"role\":\"%s\",\"width\":%u,\"height\":%u,\"format\":%u,\"result\":%u,\"device_result\":%u}\n",role,td.Width,td.Height,unsigned(td.Format),unsigned(hr),unsigned(removed));fflush(logFile);}
        return false;
    };
    if(!allocate(input,"input")||!allocate(output,"output"))return false;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;if(!allocate(keep,"original"))return false;
    td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(managed&&control.customFilter && !allocate(filtered,"prefilter"))return false;
    s.input=std::move(input);s.output=std::move(output);s.keep=std::move(keep);s.filtered=std::move(filtered);
    if(!modelRequired)return true;
    if(managed&&!DlssNrNative::Sampler::Apply(s.module,control.linearResolve!=0,control.linearColorInput!=0))return false;
    auto& p=s.parameters;p.Set("Width",td.Width);p.Set("Height",td.Height);p.Set("DLSSNR.Width",td.Width);p.Set("DLSSNR.Height",td.Height);
    p.Set("DLSSNR.Enabled",1u);p.Set("DLSSNR.Hint.Render.Preset",1u);p.Set("DLSSNR.ScalingRatio",1.0f);p.Set("DLSSNR.Intensity",1.0f);
    p.Set("DLSSNR.Style",0u);p.Set("DLSSNR.UseAutoMask",0u);p.Set("DLSSNR.UICorrection",0u);
    p.Set("DLSSNR.LocalStructureStrength",1.0f);p.Set("DLSSNR.LocalToneStrength",1.0f);p.Set("DLSSNR.SkinStructureStrength",-1.0f);
    // Per-deployment opt-in: preserve the established baseline for other games.
    const bool skinProtection=GetFileAttributesW((directory()/L"D24Dx11SkinProtection.enabled").c_str())!=INVALID_FILE_ATTRIBUTES;
    if(skinProtection){p.Set("DLSSNR.UseAutoMask",1u);p.Set("DLSSNR.SkinStructureStrength",-1.0f);p.Set("DLSSNR.UICorrection",1u);}
    event("vulkan_parity_mask1_skin_minus1_ui1",skinProtection?1:0);
    if(managed){p.Set("DLSSNR.Hint.Render.Preset",control.preset);p.Set("DLSSNR.ScalingRatio",control.networkRatio);p.Set("DLSSNR.Intensity",control.intensity);p.Set("DLSSNR.Style",control.style);p.Set("DLSSNR.LocalStructureStrength",control.localStructure);p.Set("DLSSNR.LocalToneStrength",control.localTone);p.Set("DLSSNR.SkinStructureStrength",control.skinStructure);p.Set("DLSSNR.UseAutoMask",control.autoMask);p.Set("DLSSNR.UICorrection",1u);event("d18_managed_create",1);}
    if(managed)for(auto field : {std::pair<const char*,float>{"DLSSNR.ScalingRatio",control.networkRatio},
        {"DLSSNR.Intensity",control.intensity},{"DLSSNR.LocalStructureStrength",control.localStructure},
        {"DLSSNR.LocalToneStrength",control.localTone},{"DLSSNR.SkinStructureStrength",control.skinStructure}}){
        float actual=0;auto result=p.Get(field.first,&actual);bool matches=result==NVSDK_NGX_Result_Success&&actual==field.second;
        if(control.diagnostics&&logFile)logPrint(logFile,"{\"event\":\"nr_parameter\",\"key\":\"%s\",\"requested\":%.9g,\"readback\":%.9g,\"result\":%u,\"matched\":%s}\n",field.first,field.second,actual,unsigned(result),matches?"true":"false");
        if(!matches)return false;
    }
    int code=reinterpret_cast<int(*)(void*,void*,void*,void**)>(imageBase+0x17e20)(s.common,s.context.Get(),&p,&s.handle);
    event("create",static_cast<unsigned>(code));event("width",td.Width);event("height",td.Height);event("format",td.Format);
    if(code==1&&s.handle)++s.epoch;
    return code==1&&s.handle;
}
static bool convert(Session& s,ID3D11Texture2D* input,ID3D11Texture2D* output,ID3D11Texture2D* corrected=nullptr,float dx=0,float dy=0,unsigned validWidth=0,unsigned validHeight=0){
    if(!s.convert){
        const char* source="Texture2D<float4> src:register(t0); RWTexture2D<float4> dst:register(u0); [numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){uint w,h;dst.GetDimensions(w,h);if(id.x<w&&id.y<h)dst[id.xy]=src.Load(int3(id.xy,0));}";
        ComPtr<ID3DBlob> code,errors;
        HRESULT hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr)){event("convert_compile_failed",hr);return false;}
        if(FAILED(s.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&s.convert)))return false;
    }
    if(corrected&&!s.convertJitter){
        const char* source="Texture2D<float2> src:register(t0); RWTexture2D<float2> base:register(u0); RWTexture2D<float2> nr:register(u1); cbuffer C:register(b0){float2 offset;uint vw;uint vh;} [numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){uint w,h;base.GetDimensions(w,h);if(id.x<w&&id.y<h){float2 v=src.Load(int3(id.xy,0));base[id.xy]=v;nr[id.xy]=(id.x<vw&&id.y<vh&&all(isfinite(v)))?v+offset:v;}}";
        ComPtr<ID3DBlob> code,errors;HRESULT hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr)||FAILED(s.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&s.convertJitter)))return false;
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(s.device->CreateBuffer(&bd,nullptr,&s.jitterConstants))){s.convertJitter.Reset();return false;}
    }
    ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;
    D3D11_TEXTURE2D_DESC sourceDesc{};input->GetDesc(&sourceDesc);
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;view.Format=sourceDesc.Format;
    if(view.Format==DXGI_FORMAT_R32G8X24_TYPELESS)view.Format=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    else if(view.Format==DXGI_FORMAT_R24G8_TYPELESS)view.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    else if(view.Format==DXGI_FORMAT_R32_TYPELESS)view.Format=DXGI_FORMAT_R32_FLOAT;
    else if(view.Format==DXGI_FORMAT_R16_TYPELESS)view.Format=DXGI_FORMAT_R16_UNORM;
    // NGX motion vectors are signed float pairs; a typeless allocation needs a typed SRV.
    else if(view.Format==DXGI_FORMAT_R16G16_TYPELESS)view.Format=DXGI_FORMAT_R16G16_FLOAT;
    else if(view.Format==DXGI_FORMAT_R32G32_TYPELESS)view.Format=DXGI_FORMAT_R32G32_FLOAT;
    HRESULT hr=countedSrv(s,input,&view,&srv);if(FAILED(hr)){event("convert_srv_failed",hr);return false;}
    hr=countedUav(s,output,nullptr,&uav);if(FAILED(hr)){event("convert_uav_failed",hr);return false;}
    ComPtr<ID3D11UnorderedAccessView> extra;if(corrected){
      if(FAILED(countedUav(s,corrected,nullptr,&extra)))return false;
      struct Constants{float x,y;unsigned w,h;} values{dx,dy,validWidth,validHeight};s.context->UpdateSubresource(s.jitterConstants.Get(),0,nullptr,&values,0,0);auto cb=s.jitterConstants.Get();s.context->CSSetConstantBuffers(0,1,&cb);
    }
    s.context->CSSetShader(corrected?s.convertJitter.Get():s.convert.Get(),nullptr,0);auto in=srv.Get();ID3D11UnorderedAccessView* outputs[]={uav.Get(),extra.Get()};s.context->CSSetShaderResources(0,1,&in);s.context->CSSetUnorderedAccessViews(0,corrected?2:1,outputs,nullptr);
    D3D11_TEXTURE2D_DESC td{};output->GetDesc(&td);s.context->Dispatch((td.Width+7)/8,(td.Height+7)/8,1);
    in=nullptr;outputs[0]=outputs[1]=nullptr;s.context->CSSetShaderResources(0,1,&in);s.context->CSSetUnorderedAccessViews(0,corrected?2:1,outputs,nullptr);return true;
}
static bool guideStorage(Session& s,ID3D11Texture2D* source,ComPtr<ID3D11Texture2D>& target,DXGI_FORMAT format){
 D3D11_TEXTURE2D_DESC src{},dst{};source->GetDesc(&src);if(target)target->GetDesc(&dst);
 if(!target||src.Width!=dst.Width||src.Height!=dst.Height){
  target.Reset();dst=src;dst.Format=format;dst.MipLevels=1;dst.ArraySize=1;dst.SampleDesc={1,0};dst.Usage=D3D11_USAGE_DEFAULT;
  dst.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;dst.CPUAccessFlags=0;dst.MiscFlags=0;
  if(FAILED(s.device->CreateTexture2D(&dst,nullptr,&target)))return false;
 }
 return true;
}
static bool ownGuide(Session& s,ID3D11Texture2D* source,ComPtr<ID3D11Texture2D>& target,DXGI_FORMAT format){
 return guideStorage(s,source,target,format)&&convert(s,source,target.Get());
}
static bool composePass(Session& s,unsigned mode,ID3D11Texture2D* game,ID3D11Resource* motion,unsigned flags){
 if(!s.compose){ComPtr<ID3DBlob> code,error;HRESULT hr=D3DCompile(composeSource,strlen(composeSource),nullptr,nullptr,nullptr,"CSMain","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(hr)){event("compose_compile_failed",hr);if(error&&logFile)logPrint(logFile,"%s\n",static_cast<char*>(error->GetBufferPointer()));return false;}if(FAILED(s.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&s.compose)))return false;
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(DlssNrConstants);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(s.device->CreateBuffer(&bd,nullptr,&s.constants)))return false;
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;if(FAILED(s.device->CreateSamplerState(&sd,&s.sampler)))return false;}
 DlssNrConstants c{};c.Mode=mode;c.Width=s.desc.Width;c.Height=s.desc.Height;c.WhitePoint=1;c.TransferStrength=1;c.ColourStrength=1;c.MaxRatio=4;c.Passthrough=(flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)?0u:1u;c.NetworkRatioX=c.NetworkRatioY=1;c.SourceWidth=c.Width;c.SourceHeight=c.Height;c.GuideWidth=c.Width;c.GuideHeight=c.Height;c.CompareZoom=1;c.DebugScale=1;
 static const bool vulkanParity=GetFileAttributesW((directory()/L"D24Dx11SkinProtection.enabled").c_str())!=INVALID_FILE_ATTRIBUTES;
 if(vulkanParity){c.MaxRatio=2;c.Transfer=1;}
 if(managed){c.WhitePoint=control.whitePoint;c.TransferStrength=control.transferStrength;c.ColourStrength=control.colourStrength;c.MaxRatio=control.maxRatio;c.Transfer=control.transfer;
 c.NetworkRatioX=control.networkRatio<1?float((std::max)(16u,unsigned(c.Width*control.networkRatio+0.5f)&~15u))/c.Width:1;
 c.NetworkRatioY=control.networkRatio<1?float((std::max)(8u,unsigned(c.Height*control.networkRatio+0.5f)&~7u))/c.Height:1;
 c.CatmullRomInput=control.catmullRom;c.DebugView=control.debugView;c.CompareMode=control.compare;c.CompareSwap=control.compareSwap;
 c.CompareSplit=control.compareSplit;c.CompareZoom=control.compareZoom;c.DebugScale=control.whitePoint;}
 if(managed&&control.useExposure&&s.exposure>1e-6f)c.WhitePoint=(std::min)(4096.0f,(std::max)(0.01f,s.exposurePre/s.exposure*control.whitePoint));
 s.context->UpdateSubresource(s.constants.Get(),0,nullptr,&c,0,0);
 ID3D11Resource* sources[4]={mode?static_cast<ID3D11Resource*>(s.input.Get()):game,mode?s.output.Get():nullptr,mode?s.keep.Get():nullptr,mode?motion:nullptr};ComPtr<ID3D11ShaderResourceView> views[4];ID3D11ShaderResourceView* raw[4]{};
 for(unsigned i=0;i<4;i++)if(sources[i]){HRESULT hr=countedSrv(s,sources[i],nullptr,&views[i]);if(FAILED(hr)){event("compose_srv_failed",hr);return false;}raw[i]=views[i].Get();}
 ComPtr<ID3D11UnorderedAccessView> target,keep;HRESULT hr=countedUav(s,mode==4?s.filtered.Get():mode?game:s.input.Get(),nullptr,&target);if(FAILED(hr))return false;if(!mode&&FAILED(countedUav(s,s.keep.Get(),nullptr,&keep)))return false;
 ID3D11UnorderedAccessView* outputs[]={target.Get(),keep.Get()};auto cb=s.constants.Get();auto sampler=s.sampler.Get();s.context->CSSetShader(s.compose.Get(),nullptr,0);s.context->CSSetConstantBuffers(0,1,&cb);s.context->CSSetSamplers(0,1,&sampler);s.context->CSSetShaderResources(0,4,raw);s.context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);s.context->Dispatch((c.Width+7)/8,(c.Height+7)/8,1);
 memset(raw,0,sizeof(raw));memset(outputs,0,sizeof(outputs));s.context->CSSetShaderResources(0,4,raw);s.context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);return true;
}
static void setRect(ProbeParameters& p,const char* role,unsigned w,unsigned h){std::string prefix="DLSSNR.";prefix+=role;p.Set((prefix+"SubrectBaseX").c_str(),0u);p.Set((prefix+"SubrectBaseY").c_str(),0u);p.Set((prefix+"SubrectWidth").c_str(),w);p.Set((prefix+"SubrectHeight").c_str(),h);}
static bool niohExposureProfile(){
 static const bool enabled=[](){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
  return _wcsicmp(std::filesystem::path(path).filename().c_str(),L"nioh2.exe")==0;}();return enabled;
}
static bool jitterDefault(){
#ifdef D24_JITTER_TEST_HOST
 return true;
#else
 return niohExposureProfile();
#endif
}
static bool jitterDisabledMarker(){static const bool disabled=GetFileAttributesW((directory()/L"D24NrJitterCorrection.disabled").c_str())!=INVALID_FILE_ATTRIBUTES;return disabled;}
static bool jitterRequested(const Session& s){return s.jitterMode==2||(s.jitterMode==0&&jitterDefault());}
static NrJitterSample jitterInput(Session& s,NVSDK_NGX_Parameter* game,void* owner,ID3D11DeviceContext* context,unsigned rw,unsigned rh,unsigned mw,unsigned mh,unsigned reset){
 NrJitterSample v;v.call=s.calls;v.epoch=s.epoch;v.owner=owner;v.context=context;v.rw=rw;v.rh=rh;v.mw=mw;v.mh=mh;v.ow=s.desc.Width;v.oh=s.desc.Height;v.reset=reset||!s.frames;
 using R=DlssNrNative::JitterReason;auto& status=s.jitterStatus;status.requested=jitterRequested(s)?1u:0u;status.tick=GetTickCount64();
 auto reject=[&](R reason){status.reason=reason;return v;};
 if(!status.requested)return reject(R::Off);
 if(jitterDisabledMarker())return reject(R::DisabledMarker);
 unsigned createFlags=0;
 if(game->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&createFlags)!=NVSDK_NGX_Result_Success)return reject(R::MissingFlags);
 if(!(createFlags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered))return reject(R::NotJittered);
 if(!(createFlags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes))return reject(R::NotLowRes);
 if(rw!=mw||rh!=mh)return reject(R::RegionMismatch);
 if(game->Get(NVSDK_NGX_Parameter_Jitter_Offset_X,&v.x)!=NVSDK_NGX_Result_Success||game->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y,&v.y)!=NVSDK_NGX_Result_Success)return reject(R::MissingJitter);
 if(game->Get(NVSDK_NGX_Parameter_MV_Scale_X,&v.sx)!=NVSDK_NGX_Result_Success||game->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&v.sy)!=NVSDK_NGX_Result_Success)return reject(R::MissingScale);
 if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.sx)||!std::isfinite(v.sy)||std::abs(v.sx)<=1e-6f||std::abs(v.sy)<=1e-6f)return reject(R::InvalidValues);
 v.eligible=true;status.reason=R::Waiting;
 return v;
}
// Nioh 2's captured two-texel pair is [scene scale, reciprocal gain]. This is a
// measured title profile, not a general interpretation of arbitrary 2x1 textures.
static bool decodeExposure(const float* values,bool pair,float& gain){
 gain=0;if(!std::isfinite(values[0])||values[0]<=1e-6f)return false;
 if(pair){if(!std::isfinite(values[1])||values[1]<=1e-6f||std::abs(double(values[0])*values[1]-1.0)>0.01)return false;gain=values[1];}
 else gain=values[0];return true;
}
// Injection exists only in the standalone GPU test translation unit.
#ifdef D24_EXPOSURE_TEST
static bool failExposureAllocation=false;
#endif
static void sampleExposure(Session& s,NVSDK_NGX_Parameter* game,bool pairProfile=niohExposureProfile(),bool enabled=true){
 if(!enabled)s.exposure=0;
 if(s.exposurePending){D3D11_MAPPED_SUBRESOURCE m{};auto hr=s.context->Map(s.exposureStaging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&m);
  if(hr==DXGI_ERROR_WAS_STILL_DRAWING)return;s.exposurePending=false;
  if(SUCCEEDED(hr)){float values[2]{};memcpy(values,m.pData,s.pendingExposurePair?8:4);s.context->Unmap(s.exposureStaging.Get(),0);
   float gain=0;if(enabled&&decodeExposure(values,s.pendingExposurePair,gain)){s.exposure=gain;s.exposurePre=s.pendingPre;
    static bool logged=false;if(s.pendingExposurePair&&!logged){event("nioh2_reciprocal_exposure_ready",1);logged=true;}}
   else s.exposure=0;}
 }
 if(!enabled){s.exposureStaging.Reset();return;}
 if(s.exposureAllocationFailed){s.exposure=0;return;}
 ID3D11Resource* raw=nullptr;float pre=1;game->Get(NVSDK_NGX_Parameter_ExposureTexture,&raw);
 if(!raw||game->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,&pre)!=NVSDK_NGX_Result_Success||!std::isfinite(pre)||pre<=0){s.exposure=0;return;}
 ComPtr<ID3D11Texture2D> tex;if(FAILED(raw->QueryInterface(IID_PPV_ARGS(&tex))))return;
 ComPtr<ID3D11Device> device;tex->GetDevice(&device);if(device.Get()!=s.device.Get())return;
 D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);
 const bool pair=pairProfile&&d.Width==2&&d.Height==1&&(d.Format==DXGI_FORMAT_R32_TYPELESS||d.Format==DXGI_FORMAT_R32_FLOAT);
 const bool single=d.Width==1&&d.Height==1&&d.Format==DXGI_FORMAT_R32_FLOAT;
 if((!pair&&!single)||d.MipLevels!=1||d.ArraySize!=1||d.SampleDesc.Count!=1){s.exposure=0;return;}
 if(s.exposureStaging){D3D11_TEXTURE2D_DESC old{};s.exposureStaging->GetDesc(&old);
  if(old.Width!=d.Width||old.Format!=d.Format){s.exposureStaging.Reset();s.exposure=0;}}
 if(!s.exposureStaging){d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.MiscFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ++s.exposureAllocationAttempts;
  HRESULT allocation;
#ifdef D24_EXPOSURE_TEST
  if(failExposureAllocation)allocation=E_OUTOFMEMORY;else
#endif
  allocation=s.device->CreateTexture2D(&d,nullptr,&s.exposureStaging);
  if(FAILED(allocation)){s.exposureAllocationFailed=true;s.exposure=0;
   if(logFile){logPrint(logFile,"{\"event\":\"allocation_failed\",\"role\":\"exposure_staging\",\"width\":%u,\"height\":%u,\"format\":%u,\"result\":%u,\"required\":false,\"recovery\":\"manual_white_until_restart\"}\n",d.Width,d.Height,unsigned(d.Format),unsigned(allocation));fflush(logFile);}
   return;}}
 s.context->CopyResource(s.exposureStaging.Get(),tex.Get());s.pendingPre=pre;s.pendingExposurePair=pair;s.exposurePending=true;
}
#include "dx11_capture.h"
static void diagnoseInputs(Session& s,void* owner,ID3D11DeviceContext* ctx,NVSDK_NGX_Parameter* p,unsigned flags){
 static const bool enabled=GetFileAttributesW((directory()/L"D24Dx11Diagnostics.enabled").c_str())!=INVALID_FILE_ATTRIBUTES;
 if((managed?control.diagnostics==0:!enabled)||!logFile)return;
 static uint64_t calls=0;static unsigned lastMode=~0u,lastReset=~0u,records=0;
 const auto call=++calls;unsigned reset=0;const auto resetResult=p->Get(NVSDK_NGX_Parameter_Reset,&reset);
 const unsigned mode=s.enabled?s.mode:0;
 const bool changed=mode!=lastMode||reset!=lastReset;lastMode=mode;lastReset=reset;
 if(records>=1800||(!changed&&call>16&&((call-1)%60)>=4))return;++records;
 float sx=1,sy=1,pre=1,jx=0,jy=0;unsigned rw=0,rh=0;
 p->Get(NVSDK_NGX_Parameter_MV_Scale_X,&sx);p->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&sy);
 const auto preResult=p->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,&pre);
 p->Get("Jitter.Offset.X",&jx);p->Get("Jitter.Offset.Y",&jy);
 p->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,&rw);p->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,&rh);
 const bool finite=std::isfinite(sx)&&std::isfinite(sy)&&std::isfinite(pre)&&std::isfinite(jx)&&std::isfinite(jy);
 if(!finite){sx=sy=pre=jx=jy=0;}
 logPrint(logFile,"{\"event\":\"dx11_inputs\",\"tick\":%llu,\"call\":%llu,\"mode\":%u,\"nr_frames\":%u,\"owner\":\"%p\",\"context\":\"%p\",\"flags\":%u,\"reset\":%u,\"reset_result\":%u,\"pre_result\":%u,\"pre\":%.9g,\"mv_x\":%.9g,\"mv_y\":%.9g,\"jitter_x\":%.9g,\"jitter_y\":%.9g,\"finite\":%u,\"render_width\":%u,\"render_height\":%u}\n",GetTickCount64(),call,mode,s.frames,owner,ctx,flags,reset,unsigned(resetResult),unsigned(preResult),pre,sx,sy,jx,jy,unsigned(finite),rw,rh);
 for(const char* role:{"Output","Depth","MotionVectors","ExposureTexture"}){
  ID3D11Resource* resource=nullptr;const auto result=p->Get(role,&resource);ComPtr<ID3D11Texture2D> tex;D3D11_TEXTURE2D_DESC desc{};
  if(resource&&SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&tex))))tex->GetDesc(&desc);
  logPrint(logFile,"{\"event\":\"dx11_resource\",\"call\":%llu,\"role\":\"%s\",\"result\":%u,\"resource\":\"%p\",\"width\":%u,\"height\":%u,\"format\":%u,\"bind\":%u}\n",call,role,unsigned(result),resource,desc.Width,desc.Height,unsigned(desc.Format),desc.BindFlags);
 }
 fflush(logFile);
}
static int process(Session& s,void* owner,ID3D11DeviceContext* ctx,NVSDK_NGX_Parameter* game,unsigned flags){
    diagnoseInputs(s,owner,ctx,game,flags);
    if(!s.enabled){if(managed&&s.context&&(s.exposurePending||s.exposureStaging))sampleExposure(s,game,niohExposureProfile(),false);return 0;}
    if(s.failed)return s.allocationFailure?s.allocationFailure:-1;
    if(ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return -2;
    if(s.owner&&s.owner!=owner)return 0;
    ID3D11Resource *rawOutput=nullptr,*rawDepth=nullptr,*rawMotion=nullptr;
    if(game->Get(NVSDK_NGX_Parameter_Output,&rawOutput)!=NVSDK_NGX_Result_Success||game->Get(NVSDK_NGX_Parameter_Depth,&rawDepth)!=NVSDK_NGX_Result_Success||game->Get(NVSDK_NGX_Parameter_MotionVectors,&rawMotion)!=NVSDK_NGX_Result_Success||!rawOutput||!rawDepth||!rawMotion)return -3;
    ComPtr<ID3D11Texture2D> output,depth,motion;
    if(FAILED(rawOutput->QueryInterface(IID_PPV_ARGS(&output)))||FAILED(rawDepth->QueryInterface(IID_PPV_ARGS(&depth)))||FAILED(rawMotion->QueryInterface(IID_PPV_ARGS(&motion))))return -4;
    D3D11_TEXTURE2D_DESC od{},dd{},md{};output->GetDesc(&od);depth->GetDesc(&dd);motion->GetDesc(&md);
    static D3D11_TEXTURE2D_DESC logged{};
    if(memcmp(&logged,&od,sizeof(od))){logged=od;event("game_output_width",od.Width);event("game_output_height",od.Height);event("game_output_format",od.Format);event("game_output_samples",od.SampleDesc.Count);event("game_output_array",od.ArraySize);event("game_output_mips",od.MipLevels);event("game_depth_format",dd.Format);event("game_motion_format",md.Format);}
    if(od.SampleDesc.Count!=1||od.ArraySize!=1||od.MipLevels!=1||(od.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&od.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT&&od.Format!=DXGI_FORMAT_R11G11B10_FLOAT)||od.Width>4096||od.Height>4096)return -5;
    for(const char* key:{"DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y","DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y","DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y"}){unsigned offset=0;game->Get(key,&offset);if(offset){event("unsupported_nonzero_subrect",offset);return -21;}}
    if(dd.SampleDesc.Count!=1||md.SampleDesc.Count!=1||dd.ArraySize!=1||md.ArraySize!=1)return -22;
    if(!initialize(s,ctx)){s.failed=true;return -6;}
    ComPtr<ID3D11Device> device;ctx->GetDevice(&device);if(device.Get()!=s.device.Get())return -7;
    s.owner=owner;StateScope state(s);
    if(managed&&modelDirty){if(!releaseFeature(s))return -25;modelDirty=false;}
    if(!makeFeature(s,od,s.mode==2)){s.failed=true;return s.allocationFailure?s.allocationFailure:-8;}
    if(managed)sampleExposure(s,game,niohExposureProfile(),control.useExposure!=0);
    if(managed&&s.mode==2&&control.useExposure&&niohExposureProfile()&&!s.exposureAllocationFailed&&s.exposure<=1e-6f)return 0;
    if(s.mode==1){if(!convert(s,output.Get(),s.input.Get())||!convert(s,s.input.Get(),output.Get())||!completed(s))return -20;++s.frames;if(s.frames==1||s.frames%120==0)event("conversion_frames",s.frames);return 1;}
    unsigned rw=dd.Width,rh=dd.Height;game->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,&rw);game->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,&rh);
    if(!rw||!rh||rw>dd.Width||rh>dd.Height||((flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)&&(rw>md.Width||rh>md.Height)))return -9;
    float sx=1,sy=1;game->Get(NVSDK_NGX_Parameter_MV_Scale_X,&sx);game->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&sy);
    if(!std::isfinite(sx)||!std::isfinite(sy)){event("invalid_mv_scale",1);return -23;}
    unsigned reset=0;game->Get(NVSDK_NGX_Parameter_Reset,&reset);
    if(!s.frames){event("output_bind_flags",od.BindFlags);float pre=1;auto supplied=game->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,&pre);if(logFile)logPrint(logFile,"{\"pre_exposure_supplied\":%u,\"pre_exposure\":%.9g}\n",supplied==NVSDK_NGX_Result_Success,pre);event("hdr_flag",(flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)!=0);event("guide_width",rw);event("guide_height",rh);event("depth_inverted",(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0);if(logFile)logPrint(logFile,"{\"mv_scale_x\":%.9g,\"mv_scale_y\":%.9g,\"reset\":%u}\n",sx,sy,reset);}
    // Materialize typed, private guides without changing units or valid rectangles.
    s.jitterSample=jitterInput(s,game,owner,ctx,rw,rh,md.Width,md.Height,reset);s.jitterPlan=s.jitterHistory.plan(s.jitterSample);
    if(s.jitterPlan.active)s.jitterStatus.reason=s.jitterPlan.apply?DlssNrNative::JitterReason::Active:DlssNrNative::JitterReason::HistoryReset;
    if(!ownGuide(s,depth.Get(),s.ownedDepth,DXGI_FORMAT_R32_FLOAT))return -24;
    if(s.jitterPlan.active){
      if(!guideStorage(s,motion.Get(),s.ownedMotion,DXGI_FORMAT_R32G32_FLOAT)||!guideStorage(s,motion.Get(),s.nrMotion,DXGI_FORMAT_R32G32_FLOAT)){
        const auto removed=s.device->GetDeviceRemovedReason();s.allocationFailure=FAILED(removed)?-27:-26;s.failed=true;event("jitter_guide_allocation_failed",s.allocationFailure);return s.allocationFailure;}
      if(!convert(s,motion.Get(),s.ownedMotion.Get(),s.nrMotion.Get(),s.jitterPlan.dx,s.jitterPlan.dy,rw,rh))return -24;
    }else if(!ownGuide(s,motion.Get(),s.ownedMotion,DXGI_FORMAT_R32G32_FLOAT))return -24;
    if(!s.frames)event("owned_guides_r32_rg32",1);
    rawDepth=s.ownedDepth.Get();rawMotion=s.ownedMotion.Get();
    ID3D11Resource* modelMotion=s.jitterPlan.active?s.nrMotion.Get():rawMotion;
    auto& p=s.parameters;p.Set("DLSSNR.Color",static_cast<ID3D11Resource*>(s.input.Get()));p.Set("DLSSNR.Output",static_cast<ID3D11Resource*>(s.output.Get()));p.Set("DLSSNR.Depth",rawDepth);p.Set("DLSSNR.MVec",modelMotion);
    setRect(p,"Color",od.Width,od.Height);setRect(p,"Output",od.Width,od.Height);setRect(p,"Depth",rw,rh);
    setRect(p,"MVec",(flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)?rw:md.Width,(flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)?rh:md.Height);
    p.Set("DLSSNR.MVecScaleX",sx);p.Set("DLSSNR.MVecScaleY",sy);p.Set("DLSSNR.DepthInverted",(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)?1u:0u);p.Set("DLSSNR.Reset",reset||!s.frames||s.jitterPlan.reset?1u:0u);p.Set("DLSSNR.ScalingRatio",managed?control.networkRatio:1.0f);
    // Current frame resources are borrowed only until the completion query passes.
    track(s.input.Get());track(s.output.Get());track(rawDepth);track(modelMotion);
    if(residentResources.size()>10){s.failed=true;return -10;}
    s.inflight.clear();for(void* resource:residentResources)s.inflight.emplace_back(static_cast<ID3D11Resource*>(resource));s.inflight.emplace_back(output.Get());
    static unsigned captureId=0;const unsigned frameId=++captureId;const bool capture=captureFrame();
    if(capture){captureContract(s,game,flags,frameId);captureContext(s,game,flags,frameId);
      const unsigned mw=(flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)?rw:md.Width,mh=(flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)?rh:md.Height;
      captureCrop(s,depth.Get(),frameId,"depth_raw",rw,rh);captureCrop(s,motion.Get(),frameId,"motion_raw",mw,mh);
      captureCrop(s,s.ownedDepth.Get(),frameId,"depth_owned",rw,rh);captureCrop(s,s.ownedMotion.Get(),frameId,"motion_owned",mw,mh);
      if(s.jitterPlan.active)captureCrop(s,s.nrMotion.Get(),frameId,"motion_nr",mw,mh);
    }
    if(capture)captureCrop(s,output.Get(),frameId,"sr");
    if(!composePass(s,0,output.Get(),rawMotion,flags)){s.failed=true;return -15;}
    if(managed&&control.customFilter){
        if(!composePass(s,4,output.Get(),rawMotion,flags)){s.failed=true;return -15;}
        p.Set("DLSSNR.Color",static_cast<ID3D11Resource*>(s.filtered.Get()));track(s.filtered.Get());
    }
    if(capture)captureCrop(s,managed&&control.customFilter?s.filtered.Get():s.input.Get(),frameId,"input");
    memset(pendingKernel,0,sizeof(pendingKernel));void* feature=*reinterpret_cast<void**>(s.common+0x10);
    int code=reinterpret_cast<int(*)(void*,void*,void*,void*)>(nativeVtable[0xe0/8])(nativeBackend,s.context.Get(),nullptr,feature);
    if(code){s.failed=true;return -11;}
    *reinterpret_cast<void**>(static_cast<unsigned char*>(nativeBackend)+0x140)=pendingKernel;
    code=reinterpret_cast<int(*)(void*,void*,void*,void*,void*)>(imageBase+0x18620)(s.common,s.context.Get(),s.handle,&p,nullptr);
    if(!completed(s,0))return -12;
    for(void* r:{static_cast<void*>(s.input.Get()),static_cast<void*>(s.output.Get()),static_cast<void*>(rawDepth),static_cast<void*>(modelMotion)})residentResources.erase(std::remove(residentResources.begin(),residentResources.end(),r),residentResources.end());
    if(code!=1){event("evaluate_failed",static_cast<unsigned>(code));s.failed=true;return -13;}
    if(capture)captureCrop(s,s.output.Get(),frameId,"model");
    if(capture){captureCrop(s,s.ownedDepth.Get(),frameId,"depth_after",rw,rh);captureCrop(s,s.ownedMotion.Get(),frameId,"motion_after",(flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)?rw:md.Width,(flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)?rh:md.Height);}
    if(capture&&s.jitterPlan.active)captureCrop(s,s.nrMotion.Get(),frameId,"motion_nr_after",rw,rh);
    if(!composePass(s,1,output.Get(),rawMotion,flags)){s.failed=true;return -16;}if(!completed(s,1))return -14;
    if(capture)captureCrop(s,output.Get(),frameId,"composed");
    s.inflight.clear();
    s.jitterHistory.commit(s.jitterSample,s.jitterPlan);
    const auto jitterReason=static_cast<unsigned>(s.jitterStatus.reason);
    if(managed&&control.diagnostics&&logFile&&s.jitterRecords<1800&&(s.frames<2||s.calls%120==0||s.jitterLastLogged!=jitterReason)){++s.jitterRecords;s.jitterLastLogged=jitterReason;logPrint(logFile,"{\"event\":\"dx11_jitter_state\",\"call\":%llu,\"selection\":%u,\"requested\":%u,\"reason\":%u,\"active\":%u,\"applied\":%u,\"reset\":%u,\"raw_offset_x\":%.9g,\"raw_offset_y\":%.9g}\n",s.calls,s.jitterMode,s.jitterStatus.requested,jitterReason,unsigned(s.jitterPlan.active),unsigned(s.jitterPlan.apply),unsigned(s.jitterPlan.reset),s.jitterPlan.dx,s.jitterPlan.dy);}
    ++s.frames;if((!managed||control.diagnostics)&&(s.frames==1||s.frames%120==0))event("rendered_frames",s.frames);return 1;
}
static int protectedProcess(Session& s,void* owner,ID3D11DeviceContext* ctx,NVSDK_NGX_Parameter* params,unsigned flags){
    __try{return process(s,owner,ctx,params,flags);}
    __except(EXCEPTION_EXECUTE_HANDLER){s.failed=true;s.enabled=false;event("exception_circuit_breaker",GetExceptionCode());return -99;}
}
extern "C" __declspec(dllexport) int D24Process(void* owner,ID3D11DeviceContext* ctx,NVSDK_NGX_Parameter* params,unsigned flags){
    static std::atomic<uint64_t> attempts{0};const uint64_t call=++attempts;
    auto& s=session();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock.owns_lock())return 0;s.calls=call;
    if(!logFile){logFile=_wfsopen((directory()/L"D24Native.log").c_str(),L"wb",_SH_DENYNO);event("loaded_PgUp_cycle_off_conversion_nr",1);}
    const bool down=(GetAsyncKeyState(VK_PRIOR)&0x8000)!=0;
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(!managed&&down&&!s.keyDown&&foreground==GetCurrentProcessId()&&!s.failed){s.mode=!s.enabled?1u:(s.mode==1?2u:0u);s.enabled=s.mode!=0;s.frames=0;event("mode_0_off_1_conversion_2_nr",s.mode);}
    s.keyDown=down;
    if(managed){if(s.mode!=control.mode){s.frames=0;}s.mode=control.mode;s.enabled=s.mode!=0;}
    if(!ctx||!params)return -1;
    const bool measure=managed&&control.diagnostics!=0&&!control.capture;
    if(!measure||!s.perf.enabled||s.perf.mode!=s.mode){s.perf={};s.perf.enabled=measure;s.perf.mode=s.mode;}
    const auto started=measure?perfTick():0;
    int code=protectedProcess(s,owner,ctx,params,flags);
    if(measure){const auto us=perfUs(started);s.perf.totalUs+=us;s.perf.maxFrameUs=(std::max)(s.perf.maxFrameUs,us);
      if(++s.perf.frames>=120){reportPerf(s.perf,code,s.exposureAllocationAttempts,residentResources.size(),s.inflight.size());s.perf={};s.perf.enabled=true;s.perf.mode=s.mode;}}
    if(code!=1||s.mode!=2)s.jitterHistory.clear();captureFinish(code);if(code<0){static int previous=0;if(code!=previous){event("skip_or_failure",code);previous=code;}}liveStatus.mode=s.enabled?s.mode:0;liveStatus.failed=s.failed;liveStatus.result=code;liveStatus.frames=s.frames;liveStatus.width=s.desc.Width;liveStatus.height=s.desc.Height;liveStatus.tick=GetTickCount64();return code;
}
extern "C" __declspec(dllexport) void D24SetEnabled(int enabled){auto& s=session();std::lock_guard lock(s.mutex);if(!s.failed){s.enabled=enabled!=0;s.frames=0;}}
static int releaseOwner(Session& s,void* owner){if(s.owner!=owner||s.failed)return 0;StateScope state(s);bool okay=releaseFeature(s);if(okay)s.owner=nullptr;event("owner_released",okay);return okay?1:0;}
static int protectedRelease(Session& s,void* owner){__try{return releaseOwner(s,owner);}__except(EXCEPTION_EXECUTE_HANDLER){s.failed=true;s.enabled=false;event("release_exception",GetExceptionCode());return 0;}}
extern "C" __declspec(dllexport) int D24Release(void* owner){auto& s=session();std::lock_guard lock(s.mutex);return protectedRelease(s,owner);}











extern "C" __declspec(dllexport) int D24Configure(const DlssNrNative::Settings* c){
 if(!c||c->size!=sizeof(*c)||c->version!=2||c->mode>2||!std::isfinite(c->networkRatio)||c->networkRatio<0.5f||c->networkRatio>1||c->preset>3||c->debugView>3||c->compare>2||!std::isfinite(c->compareZoom)||!std::isfinite(c->compareSplit)||c->compareZoom<1||c->compareZoom>2||c->compareSplit<0||c->compareSplit>1)return 0;
 auto& s=session();std::lock_guard lock(s.mutex);
 if(!managed||control.customFilter!=c->customFilter||control.networkRatio!=c->networkRatio||control.linearResolve!=c->linearResolve||control.linearColorInput!=c->linearColorInput||control.preset!=c->preset||control.intensity!=c->intensity||control.style!=c->style||control.localStructure!=c->localStructure||control.localTone!=c->localTone||control.skinStructure!=c->skinStructure||control.autoMask!=c->autoMask)modelDirty=true;
 if(!c->capture)captureDisarm();control=*c;managed=true;return 1;
}
// 0: per-game default, 1: explicit off, 2: explicit on. Existing ABI v2 is unchanged.
extern "C" __declspec(dllexport) int D24ConfigureJitter(uint32_t mode){
 if(mode>2)return 0;auto& s=session();std::lock_guard lock(s.mutex);
 if(s.jitterMode!=mode){s.jitterMode=mode;s.jitterStatus.reason=DlssNrNative::JitterReason::Waiting;s.jitterStatus.tick=0;}
 return 1;
}
extern "C" __declspec(dllexport) int D24ReadJitterStatus(DlssNrNative::JitterStatus* out){
 if(!out||out->size!=sizeof(*out)||out->version!=1)return 0;auto& s=session();std::lock_guard lock(s.mutex);
 *out=s.jitterStatus;out->requested=jitterRequested(s)?1u:0u;
 if(liveStatus.result!=1||liveStatus.mode!=2){out->reason=DlssNrNative::JitterReason::NrUnavailable;out->tick=liveStatus.tick;}
 return 1;
}
extern "C" __declspec(dllexport) int D24ReadStatus(DlssNrNative::Status* out){
 if(!out||out->size!=sizeof(*out)||out->version!=2)return 0;
 auto& s=session();std::lock_guard lock(s.mutex);*out=liveStatus;out->exposure=s.exposure;out->preExposure=s.exposurePre;return 1;
}
