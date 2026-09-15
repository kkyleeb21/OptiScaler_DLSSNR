#include <dlssnr/NativeSrProfile.h>
#include <dlssnr/BuildProfile.h>
#include "D18Dx11Debug.h"
#include "D18Dx11ManualState.h"
#include "D18Dx11Bindings.h"
#include "D18ContextTransaction.h"
#pragma once
#include <dlssnr/Dx11ScopedPsInput.h>
#include <dlssnr/Dx11NativeLayer.h>
#include <atomic>
#include <d3d11_4.h>
#include <detours/detours.h>
#include <mutex>
#include <cstdio>
#include <filesystem>
#include <share.h>
#include <unordered_set>
#include <array>
#include <intrin.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <cmath>
#include <proxies/NVNGX_Proxy.h>
#include <dlssnr/WildlandsSrStatus.h>
#include <dlssnr/NativeNrDx11Bridge.h>
#include <dlssnr/NativeFgDx11.h>
#include <dlssnr/WildlandsScaleQueue.h>
#include <dlssnr/Diagnostics.h>
#include <dlssnr/Dx11SameSizeColourCopy.h>
#include <dlssnr/SrResolutionContract.h>
#include <dlssnr/SrQualityMode.h>
#include <dlssnr/Dx11ColorReplay.h>
#include <dlssnr/Dx11ReplaySurface.h>
#include <dlssnr/Dx11WriterJournal.h>
#include <dlssnr/Dx11ComputeIdentity.h>
#include <dlssnr/Dx11PrivateCompute.h>
#include "D18ContextTracking.h"
#include "D18CommandListHooks.h"
#include <dlssnr/Dx11CommandListTrace.h>
#include "D18ExecutionTrace.h"

// Shared DX11 observation hooks plus the fingerprint-gated native SR/NR/FG adapter.
// Binary observation/capture remains separately opt-in; native rendering is a product path.
namespace D18InputProbe {
inline const GUID tag={0x7fcd24a8,0x4c72,0x4ecd,{0x9e,0xa1,0x93,0x4b,0x0d,0x47,0xd6,0x28}};
inline std::mutex guard;
inline FILE* output=nullptr;
inline bool probeReady=false;
template<class... Args> inline void LogInput(const char* format,Args... args){if(output)fprintf(output,format,args...);}
inline void FlushInput(){if(output)fflush(output);}
inline FILE* shaderArchive=nullptr;
inline unsigned long long archiveBytes=0;
inline std::unordered_set<unsigned long long> archived,seen;
inline ID3D11Device* observedDevice=nullptr;
inline unsigned phase=0,phaseSamples=0;
inline int openedPhase=-1;
inline bool sweepActive=false;
inline ULONGLONG stageEnd[2]{};
inline std::atomic<unsigned long long> commandLists{0};
inline bool installed=false,keyDown=false;
inline std::atomic<bool> frameCounting{false};
inline std::atomic<unsigned long long> frameVisits{0},lockDrops{0},timeDrops{0},quotaDrops{0};
inline unsigned long long frameNumber=0,previousVisits=0;
inline std::atomic<unsigned long long> skipVisits{0};
inline unsigned stageSamples[2]{};
inline unsigned long long selectedDeviceId=0;

inline std::atomic<bool> sampleOpen{false};
inline void Pause(){sampleOpen.store(false,std::memory_order_relaxed);frameCounting.store(false,std::memory_order_relaxed);}
inline ID3D11DeviceContext* selectedContext=nullptr;
inline bool contextCovered=false;
inline unsigned deviceEvents=0;
struct ShaderInfo { unsigned long long hash=0; unsigned bytes=0,role=0; unsigned long long deviceId=0; };
inline std::atomic<unsigned> identified{0};
inline std::atomic<unsigned long long> hashedBytes{0};
using SetShader=void(WINAPI*)(ID3D11DeviceContext*,ID3D11PixelShader*,ID3D11ClassInstance* const*,UINT);
inline SetShader setShader=nullptr;
inline void* hookedDrawEntry=nullptr;
using Execute=void(WINAPI*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
namespace Wildlands::PostReplay {inline void Mutation(ID3D11DeviceContext*,ID3D11Resource*,const char*);inline void CommandList(ID3D11DeviceContext*,ID3D11CommandList*,BOOL,const void*);inline void CommandListFinished(ID3D11DeviceContext*);}
inline Execute execute=nullptr;
inline void WINAPI OnExecute(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore){
    CommandLists::Record<58>(c,list,restore);
    ScopedExecuteDepth depth;
    D18ExecutionTrace::Span trace(D18ExecutionTrace::executeBudget,"execute_begin","execute_end",c);
    trace.Step("execute_restore_flag",0,restore);
    if(sampleOpen.load(std::memory_order_relaxed))++commandLists;
    D18ContextTransaction::Scope transaction(c,2);
    Wildlands::PostReplay::CommandList(c,list,restore,_ReturnAddress());
    execute(c,list,restore);
    if(transaction)Wildlands::PostReplay::CommandListFinished(c);
    boundContext=nullptr;boundShader=nullptr;
    computeContext=nullptr;boundCompute=nullptr;
}
inline ULONGLONG keyTick=0,armedUntil=0,lastSample=0;
inline unsigned samples=0,sessions=0;
inline std::atomic<unsigned long long> created{0},matched{0},draws{0};
using Create=HRESULT(WINAPI*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
using Draw=void(WINAPI*)(ID3D11DeviceContext*,UINT,UINT);
using Indexed=void(WINAPI*)(ID3D11DeviceContext*,UINT,UINT,INT);
using Instanced=void(WINAPI*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
using IndexedInstanced=void(WINAPI*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using Auto=void(WINAPI*)(ID3D11DeviceContext*);
using Indirect=void(WINAPI*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);
inline Create create=nullptr;
inline Draw draw=nullptr;inline Indexed indexed=nullptr;
inline Instanced instanced=nullptr;inline IndexedInstanced indexedInstanced=nullptr;
inline Auto automatic=nullptr;inline Indirect indirect=nullptr,indexedIndirect=nullptr;
inline void CreationStats();
inline unsigned long long ResourceId(ID3D11Resource* r);
#include "D18InputProbeReadback.inl"
inline std::filesystem::path ProbeRoot();
#include "D18WildlandsSr.inl"

inline void BeforePresent(IDXGISwapChain* nativeChain,const void* chain){
 if(!DlssNr::WildlandsSr::postReplay||Wildlands::presentOwner!=chain)return;
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock())return;
 DlssNr::Dx11CommandListTrace::journal.Mark(selectedContext,"present_before",nativeChain);
}
inline void Frame(ID3D11Device* device,const void* chain=nullptr,bool primary=false,IDXGISwapChain* nativeChain=nullptr) {
    std::unique_lock lock(guard,std::try_to_lock);
    if(!lock.owns_lock()||!installed||!probeReady)return;
    if(Wildlands::enabled && (device!=observedDevice||!Wildlands::AcceptPresent(chain,primary)))return;
    ID3D11DeviceContext* current=nullptr;device->GetImmediateContext(&current);
    if(current!=selectedContext){
        sampleOpen.store(false,std::memory_order_relaxed);
        if(selectedContext)selectedContext->Release();
        selectedContext=current;
        contextCovered=(*reinterpret_cast<void***>(current))[13]==hookedDrawEntry;
        if(deviceEvents++<8){LogInput("{\"event\":\"input_present_device\",\"feature_level\":%u,\"draw_implementation_covered\":%s}\n",unsigned(device->GetFeatureLevel()),contextCovered?"true":"false");FlushInput();}
    } else current->Release();
    if(!contextCovered)return;
    D18ContextTransaction::Scope transaction(current ? current : selectedContext,3);
    if(!transaction)return;
    D18Dx11Debug::Poll(device);
    if(DlssNr::WildlandsSr::offscreenUpscale && nativeChain){
        DXGI_SWAP_CHAIN_DESC desc{};
        if(SUCCEEDED(nativeChain->GetDesc(&desc))){Wildlands::displayWidth=desc.BufferDesc.Width;Wildlands::displayHeight=desc.BufferDesc.Height;}
        else {Wildlands::displayWidth=0;Wildlands::displayHeight=0;}
    }
    DlssNr::Dx11CommandListTrace::journal.Mark(selectedContext,"present_after",nativeChain);
    DlssNr::Dx11CommandListTrace::journal.End(frameNumber,Wildlands::PostReplay::invalid.load());
    Wildlands::Frame();
    if(Wildlands::enabled){++frameNumber;if(DlssNr::WildlandsSr::postReplay&&!DlssNr::WildlandsSr::nativeHandoff&&DlssNr::WildlandsSr::enabled)DlssNr::Dx11CommandListTrace::journal.Begin(ProbeRoot(),frameNumber,selectedContext);return;}
    Numeric::Poll(selectedContext);
    const auto now=GetTickCount64();
    if(now-keyTick>=16){
        keyTick=now;DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        const bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0 && foreground==GetCurrentProcessId();
        if(down&&!keyDown&&sessions<3){
            ++sessions;samples=0;openedPhase=-1;seen.clear();armedUntil=now+15000;lastSample=0;sampleOpen.store(false,std::memory_order_relaxed);
            LogInput("{\"event\":\"input_probe_armed\",\"session\":%u,\"tick\":%llu,\"created\":%llu,\"matched\":%llu,\"observed_draw_visits\":%llu}\n",sessions,now,created.load(),matched.load(),draws.load());FlushInput();
        }
        keyDown=down;
    }
    if(armedUntil && now>=armedUntil){
        LogInput("{\"event\":\"input_probe_end\",\"session\":%u,\"samples\":%u,\"matched\":%llu,\"observed_draw_visits\":%llu}\n",sessions,samples,matched.load(),draws.load());Numeric::Stats();CreationStats();FlushInput();if(shaderArchive)fflush(shaderArchive);LogInput("{\"event\":\"input_command_lists\",\"count\":%llu}\n",commandLists.load());FlushInput();armedUntil=0;sampleOpen.store(false,std::memory_order_relaxed);
    }
    Pause();
    const auto visits=frameVisits.exchange(0);
    if(visits)previousVisits=visits;
    ++frameNumber;
    if(sweepActive){LogInput("{\"event\":\"input_sweep_end\",\"phase\":%u,\"frame\":%llu,\"samples\":%u,\"ps\":%u,\"cs\":%u,\"visits\":%llu,\"skip_visits\":%llu,\"lock_drops\":%llu,\"time_drops\":%llu,\"quota_drops\":%llu}\n",phase,frameNumber-1,phaseSamples,stageSamples[0],stageSamples[1],visits,skipVisits.load(),lockDrops.exchange(0),timeDrops.exchange(0),quotaDrops.exchange(0));FlushInput();sweepActive=false;}
    if(armedUntil&&now<armedUntil&&samples<864){
        const auto group=static_cast<unsigned>((now-(armedUntil-15000))/5000);
        const int first=static_cast<int>(group*3);
        if(openedPhase<first+2){
            phase=static_cast<unsigned>(openedPhase<first?first:openedPhase+1);
            openedPhase=static_cast<int>(phase);seen.clear();phaseSamples=0;
            stageSamples[0]=stageSamples[1]=0;stageEnd[0]=stageEnd[1]=0;sweepActive=true;
            skipVisits=previousVisits*(phase%3)/3;
            sampleOpen.store(true,std::memory_order_relaxed);
        }
        frameCounting.store(true,std::memory_order_relaxed);
    }

}
inline void WINAPI OnSetShader(ID3D11DeviceContext* ctx,ID3D11PixelShader* shader,ID3D11ClassInstance* const* classes,UINT count){CommandLists::Record<9>(ctx,shader,classes,count);
    setShader(ctx,shader,classes,count);
    boundShader=shader;boundContext=ctx;
}
inline const GUID resourceTag={0x507f4d21,0xaf63,0x4301,{0x90,0xce,0x81,0x51,0x72,0x44,0x20,0x12}};
inline unsigned long long resourceSerial=0;
inline unsigned long long ResourceId(ID3D11Resource* r){unsigned long long id=0;UINT n=sizeof(id);if(FAILED(r->GetPrivateData(resourceTag,&n,&id))){id=++resourceSerial;if(FAILED(r->SetPrivateData(resourceTag,sizeof(id),&id)))return 0;}return id;}
inline void ViewWords(const void* desc,size_t bytes){LogInput(",\"view_desc_words\":[");for(size_t i=0;i<bytes/sizeof(UINT);++i){UINT word=0;memcpy(&word,static_cast<const char*>(desc)+i*sizeof(UINT),sizeof(word));LogInput("%s%u",i?",":"",word);}fputc(']',output);}
inline void Observe(ID3D11DeviceContext* ctx,const char* method,const void* caller,bool compute=false) {
    if(internalWork)return;
    if(!compute)Numeric::OnDraw(ctx);
    if(Numeric::enabled.load()||Wildlands::enabled.load())return; // Targeted values replace broad metadata sweeps when explicitly enabled.
    if(!frameCounting.load(std::memory_order_relaxed))return;
    const auto visit=++frameVisits;++draws;
    if(!sampleOpen.load(std::memory_order_relaxed)||visit<=skipVisits)return;
    std::unique_lock lock(guard,std::try_to_lock);
    if(!lock.owns_lock()){++lockDrops;return;}
    if(!output||!sampleOpen.load(std::memory_order_relaxed))return;
    const auto now=GetTickCount64();
    if(!armedUntil||now>=armedUntil||samples>=864||phaseSamples>=96){++quotaDrops;sampleOpen.store(false,std::memory_order_relaxed);return;}
    if(stageSamples[compute?1:0]>=48){++quotaDrops;return;}
    const unsigned stageIndex=compute?1:0;
    if(!stageEnd[stageIndex])stageEnd[stageIndex]=now+12;
    if(now>stageEnd[stageIndex]){++timeDrops;return;}
    ID3D11Device* owner=nullptr;ctx->GetDevice(&owner);
    const bool sameDevice=owner==observedDevice;if(owner)owner->Release();if(!sameDevice)return;
    ID3D11DeviceChild* shader=nullptr;
    if(compute){ID3D11ComputeShader* cs=nullptr;ctx->CSGetShader(&cs,nullptr,nullptr);shader=cs;}
    else {ID3D11PixelShader* ps=nullptr;ctx->PSGetShader(&ps,nullptr,nullptr);shader=ps;}
    if(!shader)return;
    ShaderInfo info{};UINT bytes=sizeof(info);
    const bool known=SUCCEEDED(shader->GetPrivateData(tag,&bytes,&info));
    const auto shaderId=reinterpret_cast<unsigned long long>(shader);
    const auto identity=(known?info.hash:shaderId)^reinterpret_cast<unsigned long long>(ctx);
    shader->Release();
    if(!seen.insert(identity).second)return;
    const unsigned role=info.role; // role 0 is unidentified; never inferred to be depth/motion.
    lastSample=now;++samples;++phaseSamples;++stageSamples[compute?1:0];
    LogInput("{\"event\":\"input_binding\",\"session\":%u,\"sample\":%u,\"tick\":%llu,\"role\":%u,\"draw\":\"%s\",\"context_type\":%u,\"shader_observed_at_create\":%s,\"shader_hash\":\"%016llx\",\"shader_bytes\":%u,\"stage\":\"%s\",\"caller\":\"%p\",\"sweep\":%u,\"context_id\":\"%p\",\"shader_id\":\"%016llx\",\"frame\":%llu,\"device_id\":%llu,\"textures\":[",sessions,samples,now,role,method,unsigned(ctx->GetType()),known?"true":"false",info.hash,info.bytes,compute?"CS":"PS",caller,phase,static_cast<void*>(ctx),shaderId,frameNumber,selectedDeviceId);
    ID3D11ShaderResourceView* views[128]{};if(compute)ctx->CSGetShaderResources(0,128,views);else ctx->PSGetShaderResources(0,128,views);
    for(unsigned i=0;i<128;++i){
        if(i)fputc(',',output);
        LogInput("{\"slot\":%u,\"present\":%s",i,views[i]?"true":"false");
        if(views[i]){
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};views[i]->GetDesc(&vd);
            ID3D11Resource* resource=nullptr;views[i]->GetResource(&resource);
            ViewWords(&vd,sizeof(vd));if(resource)LogInput(",\"resource_id\":\"%p\",\"resource_uid\":%llu",static_cast<void*>(resource),ResourceId(resource));
            ID3D11Texture2D* texture=nullptr;
            if(resource&&SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&texture)))){
                D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
                LogInput(",\"id\":\"%p\",\"width\":%u,\"height\":%u,\"format\":%u,\"view_format\":%u,\"view_dimension\":%u,\"mips\":%u,\"array\":%u,\"msaa\":%u",static_cast<void*>(resource),d.Width,d.Height,unsigned(d.Format),unsigned(vd.Format),unsigned(vd.ViewDimension),d.MipLevels,d.ArraySize,d.SampleDesc.Count);
                texture->Release();
            }
            if(resource)resource->Release();views[i]->Release();
        }
        fputc('}',output);
    }
    LogInput("],\"constant_buffers\":[");
    for(unsigned slot=0;slot<14;++slot){
        const UINT index=slot;
        ID3D11Buffer* buffer=nullptr;if(compute)ctx->CSGetConstantBuffers(index,1,&buffer);else ctx->PSGetConstantBuffers(index,1,&buffer);
        D3D11_BUFFER_DESC desc{};if(buffer)buffer->GetDesc(&desc);
        LogInput("%s{\"slot\":%u,\"id\":\"%p\",\"bytes\":%u}",slot?",":"",index,static_cast<void*>(buffer),desc.ByteWidth);
        if(buffer)buffer->Release();
    }
    LogInput("],\"render_targets\":[");
    ID3D11RenderTargetView* targets[8]{};ID3D11DepthStencilView* depth=nullptr;
    ctx->OMGetRenderTargets(8,targets,&depth);
    for(unsigned i=0;i<9;++i){
        ID3D11View* view=i<8?static_cast<ID3D11View*>(targets[i]):static_cast<ID3D11View*>(depth);
        if(i)fputc(',',output);LogInput("{\"slot\":%u,\"present\":%s",i,view?"true":"false");
        if(view){
            if(i<8){D3D11_RENDER_TARGET_VIEW_DESC vd{};targets[i]->GetDesc(&vd);ViewWords(&vd,sizeof(vd));}
            else {D3D11_DEPTH_STENCIL_VIEW_DESC vd{};depth->GetDesc(&vd);ViewWords(&vd,sizeof(vd));}
            ID3D11Resource* resource=nullptr;view->GetResource(&resource);if(resource)LogInput(",\"resource_id\":\"%p\",\"resource_uid\":%llu",static_cast<void*>(resource),ResourceId(resource));ID3D11Texture2D* texture=nullptr;
            if(resource&&SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&texture)))){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
                LogInput(",\"id\":\"%p\",\"width\":%u,\"height\":%u,\"format\":%u",static_cast<void*>(resource),desc.Width,desc.Height,unsigned(desc.Format));texture->Release();}
            if(resource)resource->Release();view->Release();}fputc('}',output);
    }
    LogInput("],\"compute_uavs\":[");
    ID3D11UnorderedAccessView* uavs[8]{};ctx->CSGetUnorderedAccessViews(0,8,uavs);
    for(unsigned i=0;i<8;++i){if(i)fputc(',',output);LogInput("{\"slot\":%u,\"present\":%s",i,uavs[i]?"true":"false");
        if(uavs[i]){D3D11_UNORDERED_ACCESS_VIEW_DESC vd{};uavs[i]->GetDesc(&vd);ViewWords(&vd,sizeof(vd));ID3D11Resource* resource=nullptr;uavs[i]->GetResource(&resource);if(resource)LogInput(",\"resource_id\":\"%p\",\"resource_uid\":%llu",static_cast<void*>(resource),ResourceId(resource));ID3D11Texture2D* texture=nullptr;
            if(resource&&SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&texture)))){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);LogInput(",\"width\":%u,\"height\":%u,\"format\":%u",desc.Width,desc.Height,unsigned(desc.Format));texture->Release();}
            if(resource)resource->Release();uavs[i]->Release();}fputc('}',output);}
    D3D11_VIEWPORT viewport{};UINT count=1;ctx->RSGetViewports(&count,&viewport);
    LogInput("],\"upstream\":[");
    if(!compute){
        ID3D11VertexShader* vs=nullptr;ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;ID3D11GeometryShader* gs=nullptr;
        ctx->VSGetShader(&vs,nullptr,nullptr);ctx->HSGetShader(&hs,nullptr,nullptr);ctx->DSGetShader(&ds,nullptr,nullptr);ctx->GSGetShader(&gs,nullptr,nullptr);
        ID3D11DeviceChild* upstream[]={vs,hs,ds,gs};const char* names[]={"VS","HS","DS","GS"};
        for(unsigned i=0;i<4;++i){if(i)fputc(',',output);ShaderInfo u{};UINT n=sizeof(u);const bool tagged=upstream[i]&&SUCCEEDED(upstream[i]->GetPrivateData(tag,&n,&u));
            LogInput("{\"stage\":\"%s\",\"present\":%s,\"id\":\"%p\",\"known\":%s,\"hash\":\"%016llx\",\"device_id\":%llu,\"constant_buffers\":[",names[i],upstream[i]?"true":"false",static_cast<void*>(upstream[i]),tagged?"true":"false",u.hash,u.deviceId);
            if(upstream[i]){ID3D11Buffer* bs[14]{};
                if(i==0)ctx->VSGetConstantBuffers(0,14,bs);else if(i==1)ctx->HSGetConstantBuffers(0,14,bs);else if(i==2)ctx->DSGetConstantBuffers(0,14,bs);else ctx->GSGetConstantBuffers(0,14,bs);
                for(unsigned j=0;j<14;++j){D3D11_BUFFER_DESC bd{};if(bs[j])bs[j]->GetDesc(&bd);LogInput("%s{\"slot\":%u,\"id\":\"%p\",\"bytes\":%u}",j?",":"",j,static_cast<void*>(bs[j]),bd.ByteWidth);if(bs[j])bs[j]->Release();}
                upstream[i]->Release();
            }LogInput("]}");
        }
    }
    LogInput("],\"om_write_masks\":[");
    if(!compute){ID3D11BlendState* blend=nullptr;FLOAT factors[4]{};UINT mask=0;ctx->OMGetBlendState(&blend,factors,&mask);D3D11_BLEND_DESC bd{};if(blend)blend->GetDesc(&bd);
        for(unsigned i=0;i<8;++i)LogInput("%s%u",i?",":"",blend?unsigned(bd.RenderTarget[bd.IndependentBlendEnable?i:0].RenderTargetWriteMask):15u);if(blend)blend->Release();}
    LogInput("],\"draw_visit\":%llu,\"shader_device_id\":%llu,\"viewport\":{\"count\":%u,\"width\":%.3f,\"height\":%.3f}}\n",visit,info.deviceId,count,viewport.Width,viewport.Height);FlushInput();
}
#include "D18InputProbeCreation.inl"
using SetCompute=void(WINAPI*)(ID3D11DeviceContext*,ID3D11ComputeShader*,ID3D11ClassInstance* const*,UINT);
using Dispatch=void(WINAPI*)(ID3D11DeviceContext*,UINT,UINT,UINT);
inline SetCompute setCompute=nullptr,deferredSetCompute=nullptr;
inline Dispatch dispatch=nullptr,deferredDispatch=nullptr;
inline Indirect dispatchIndirect=nullptr,deferredDispatchIndirect=nullptr;
inline bool attachCS=false,attachDispatch=false,attachDispatchIndirect=false;
inline void WINAPI OnSetCompute(ID3D11DeviceContext* c,ID3D11ComputeShader* p,ID3D11ClassInstance* const* a,UINT n){CommandLists::Record<69>(c,p,a,n);setCompute(c,p,a,n);boundCompute=p;computeContext=c;}
inline void WINAPI OnDeferredSetCompute(ID3D11DeviceContext* c,ID3D11ComputeShader* p,ID3D11ClassInstance* const* a,UINT n){CommandLists::Record<69>(c,p,a,n);deferredSetCompute(c,p,a,n);boundCompute=p;computeContext=c;}
inline void WINAPI OnDispatch(ID3D11DeviceContext* c,UINT a,UINT b,UINT d){CommandLists::Record<41>(c,a,b,d);Observe(c,"Dispatch",_ReturnAddress(),true);Wildlands::PostReplay::ComputeMutation(c,a,b,d);dispatch(c,a,b,d);}
inline void WINAPI OnDeferredDispatch(ID3D11DeviceContext* c,UINT a,UINT b,UINT d){CommandLists::Record<41>(c,a,b,d);Observe(c,"DeferredDispatch",_ReturnAddress(),true);deferredDispatch(c,a,b,d);}
inline void WINAPI OnDispatchIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o){CommandLists::Record<42>(c,b,o);Observe(c,"DispatchIndirect",_ReturnAddress(),true);Wildlands::PostReplay::ComputeMutation(c,0,0,0,true);dispatchIndirect(c,b,o);}
inline void WINAPI OnDeferredDispatchIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o){CommandLists::Record<42>(c,b,o);Observe(c,"DeferredDispatchIndirect",_ReturnAddress(),true);deferredDispatchIndirect(c,b,o);}
inline void WINAPI OnDraw(ID3D11DeviceContext* c,UINT a,UINT b){CommandLists::Record<13>(c,a,b);Observe(c,"Draw",_ReturnAddress());{Wildlands::PostReplay::NativeHandoff handoff(c,{Dx11DrawReplay::Kind::Draw,a,b});draw(c,a,b);}if(a)Wildlands::AfterDraw(c,{Dx11DrawReplay::Kind::Draw,a,b});}
inline void WINAPI OnIndexed(ID3D11DeviceContext* c,UINT a,UINT b,INT d){CommandLists::Record<12>(c,a,b,d);Observe(c,"DrawIndexed",_ReturnAddress());{Wildlands::PostReplay::NativeHandoff handoff(c,{Dx11DrawReplay::Kind::Indexed,a,b,1,0,d});indexed(c,a,b,d);}if(a)Wildlands::AfterDraw(c,{Dx11DrawReplay::Kind::Indexed,a,b,1,0,d});}
inline void WINAPI OnInstanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,UINT e){CommandLists::Record<21>(c,a,b,d,e);Observe(c,"DrawInstanced",_ReturnAddress());{Wildlands::PostReplay::NativeHandoff handoff(c,{Dx11DrawReplay::Kind::Instanced,a,d,b,e});instanced(c,a,b,d,e);}Wildlands::AfterDraw(c,{Dx11DrawReplay::Kind::Instanced,a,d,b,e});}
inline void WINAPI OnIndexedInstanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,INT e,UINT f){CommandLists::Record<20>(c,a,b,d,e,f);Observe(c,"DrawIndexedInstanced",_ReturnAddress());{Wildlands::PostReplay::NativeHandoff handoff(c,{Dx11DrawReplay::Kind::IndexedInstanced,a,d,b,f,e});indexedInstanced(c,a,b,d,e,f);}Wildlands::AfterDraw(c,{Dx11DrawReplay::Kind::IndexedInstanced,a,d,b,f,e});}
inline void WINAPI OnAuto(ID3D11DeviceContext* c){CommandLists::Record<38>(c);Observe(c,"DrawAuto",_ReturnAddress());automatic(c);Wildlands::AfterDraw(c);}
inline void WINAPI OnIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o){CommandLists::Record<40>(c,b,o);Observe(c,"DrawInstancedIndirect",_ReturnAddress());indirect(c,b,o);Wildlands::AfterDraw(c);}
inline void WINAPI OnIndexedIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o){CommandLists::Record<39>(c,b,o);Observe(c,"DrawIndexedInstancedIndirect",_ReturnAddress());indexedIndirect(c,b,o);Wildlands::AfterDraw(c);}

inline SetShader deferred_setShader=nullptr;
inline bool attach_setShader=false;
inline void WINAPI Deferred_setShader(ID3D11DeviceContext* c,ID3D11PixelShader* p,ID3D11ClassInstance* const* a,UINT n){CommandLists::Record<9>(c,p,a,n);deferred_setShader(c,p,a,n);boundContext=c;boundShader=p;}
inline Draw deferred_draw=nullptr;
inline bool attach_draw=false;
inline void WINAPI Deferred_draw(ID3D11DeviceContext* c,UINT a,UINT b){CommandLists::Record<13>(c,a,b);Observe(c,"deferred_draw",_ReturnAddress());deferred_draw(c,a,b);}
inline Indexed deferred_indexed=nullptr;
inline bool attach_indexed=false;
inline void WINAPI Deferred_indexed(ID3D11DeviceContext* c,UINT a,UINT b,INT d){CommandLists::Record<12>(c,a,b,d);Observe(c,"deferred_indexed",_ReturnAddress());deferred_indexed(c,a,b,d);}
inline Instanced deferred_instanced=nullptr;
inline bool attach_instanced=false;
inline void WINAPI Deferred_instanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,UINT e){CommandLists::Record<21>(c,a,b,d,e);Observe(c,"deferred_instanced",_ReturnAddress());deferred_instanced(c,a,b,d,e);}
inline IndexedInstanced deferred_indexedInstanced=nullptr;
inline bool attach_indexedInstanced=false;
inline void WINAPI Deferred_indexedInstanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,INT e,UINT f){CommandLists::Record<20>(c,a,b,d,e,f);Observe(c,"deferred_indexedInstanced",_ReturnAddress());deferred_indexedInstanced(c,a,b,d,e,f);}
inline Auto deferred_automatic=nullptr;
inline bool attach_automatic=false;
inline void WINAPI Deferred_automatic(ID3D11DeviceContext* c){CommandLists::Record<38>(c);Observe(c,"deferred_automatic",_ReturnAddress());deferred_automatic(c);}
inline Indirect deferred_indirect=nullptr;
inline bool attach_indirect=false;
inline void WINAPI Deferred_indirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o){CommandLists::Record<40>(c,b,o);Observe(c,"deferred_indirect",_ReturnAddress());deferred_indirect(c,b,o);}
inline Indirect deferred_indexedIndirect=nullptr;
inline bool attach_indexedIndirect=false;
inline void WINAPI Deferred_indexedIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o){CommandLists::Record<39>(c,b,o);Observe(c,"deferred_indexedIndirect",_ReturnAddress());deferred_indexedIndirect(c,b,o);}

#include "D18PostMutationHooks.inl"
inline void Install(ID3D11Device* device){
    EarlyDevice(device);
    std::lock_guard lock(guard);if(installed||!device||!probeReady)return;
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
    const auto name=std::filesystem::path(exe).filename().wstring();
    const auto root=ProbeRoot();selectedDeviceId=DeviceId(device);
    ID3D11DeviceContext* context=nullptr;device->GetImmediateContext(&context);
    auto cv=*reinterpret_cast<void***>(context);
    observedDevice=device;device->AddRef();Numeric::Initialize(device,root);
    Wildlands::map=reinterpret_cast<Wildlands::MapFn>(cv[14]);Wildlands::unmap=reinterpret_cast<Wildlands::UnmapFn>(cv[15]);Wildlands::update=reinterpret_cast<Wildlands::UpdateFn>(cv[48]);
    execute=reinterpret_cast<Execute>(cv[58]);
    PostMutationHooks::CopyResource=reinterpret_cast<PostMutationHooks::CopyResourceFn>(cv[47]);
    PostMutationHooks::CopySubresource=reinterpret_cast<PostMutationHooks::CopySubresourceFn>(cv[46]);
    PostMutationHooks::ClearTarget=reinterpret_cast<PostMutationHooks::ClearTargetFn>(cv[50]);
    PostMutationHooks::ClearUint=reinterpret_cast<PostMutationHooks::ClearUintFn>(cv[51]);
    PostMutationHooks::ClearFloat=reinterpret_cast<PostMutationHooks::ClearFloatFn>(cv[52]);
    PostMutationHooks::Resolve=reinterpret_cast<PostMutationHooks::ResolveFn>(cv[57]);
    PostMutationHooks::GenerateMips=reinterpret_cast<PostMutationHooks::GenerateMipsFn>(cv[54]);
    setCompute=reinterpret_cast<SetCompute>(cv[69]);
    dispatch=reinterpret_cast<Dispatch>(cv[41]);dispatchIndirect=reinterpret_cast<Indirect>(cv[42]);
    ID3D11DeviceContext* deferred=nullptr;
    const HRESULT deferredStatus=device->CreateDeferredContext(0,&deferred);
    if(SUCCEEDED(deferredStatus)&&deferred){auto dc=*reinterpret_cast<void***>(deferred);
        deferredSetCompute=reinterpret_cast<SetCompute>(dc[69]);attachCS=dc[69]!=cv[69];
        deferredDispatch=reinterpret_cast<Dispatch>(dc[41]);attachDispatch=dc[41]!=cv[41];
        deferredDispatchIndirect=reinterpret_cast<Indirect>(dc[42]);attachDispatchIndirect=dc[42]!=cv[42];
        deferred_setShader=reinterpret_cast<SetShader>(dc[9]);attach_setShader=dc[9]!=cv[9];
        deferred_draw=reinterpret_cast<Draw>(dc[13]);attach_draw=dc[13]!=cv[13];
        deferred_indexed=reinterpret_cast<Indexed>(dc[12]);attach_indexed=dc[12]!=cv[12];
        deferred_instanced=reinterpret_cast<Instanced>(dc[21]);attach_instanced=dc[21]!=cv[21];
        deferred_indexedInstanced=reinterpret_cast<IndexedInstanced>(dc[20]);attach_indexedInstanced=dc[20]!=cv[20];
        deferred_automatic=reinterpret_cast<Auto>(dc[38]);attach_automatic=dc[38]!=cv[38];
        deferred_indirect=reinterpret_cast<Indirect>(dc[40]);attach_indirect=dc[40]!=cv[40];
        deferred_indexedIndirect=reinterpret_cast<Indirect>(dc[39]);attach_indexedIndirect=dc[39]!=cv[39];
        CommandLists::Register(9,dc[9]);CommandLists::Register(69,dc[69]);
        CommandLists::Register(12,dc[12]);
        CommandLists::Register(13,dc[13]);
        CommandLists::Register(20,dc[20]);
        CommandLists::Register(21,dc[21]);
        CommandLists::Register(38,dc[38]);
        CommandLists::Register(39,dc[39]);
        CommandLists::Register(40,dc[40]);
        CommandLists::Register(41,dc[41]);
        CommandLists::Register(42,dc[42]);
        deferred->Release();
    }
    hookedDrawEntry=cv[13];setShader=reinterpret_cast<SetShader>(cv[9]);
    draw=reinterpret_cast<Draw>(cv[13]);indexed=reinterpret_cast<Indexed>(cv[12]);
    indexedInstanced=reinterpret_cast<IndexedInstanced>(cv[20]);instanced=reinterpret_cast<Instanced>(cv[21]);automatic=reinterpret_cast<Auto>(cv[38]);indexedIndirect=reinterpret_cast<Indirect>(cv[39]);indirect=reinterpret_cast<Indirect>(cv[40]);context->Release();
    LONG status=DetourTransactionBegin();
    if(status==NO_ERROR){
        status=DetourUpdateThread(GetCurrentThread());
        #define D18_ATTACH(p,h) if(status==NO_ERROR)status=DetourAttach(reinterpret_cast<PVOID*>(&(p)),h)
        D18_ATTACH(setShader,OnSetShader);D18_ATTACH(draw,OnDraw);D18_ATTACH(indexed,OnIndexed);D18_ATTACH(instanced,OnInstanced);D18_ATTACH(indexedInstanced,OnIndexedInstanced);D18_ATTACH(automatic,OnAuto);D18_ATTACH(indirect,OnIndirect);D18_ATTACH(indexedIndirect,OnIndexedIndirect);
        if(Wildlands::enabled){D18_ATTACH(Wildlands::map,Wildlands::OnMap);D18_ATTACH(Wildlands::unmap,Wildlands::OnUnmap);D18_ATTACH(Wildlands::update,Wildlands::OnUpdate);}
        D18_ATTACH(execute,OnExecute);
        if(DlssNr::WildlandsSr::postReplay){D18_ATTACH(PostMutationHooks::CopyResource,PostMutationHooks::OnCopyResource);D18_ATTACH(PostMutationHooks::CopySubresource,PostMutationHooks::OnCopySubresource);D18_ATTACH(PostMutationHooks::ClearTarget,PostMutationHooks::OnClearTarget);D18_ATTACH(PostMutationHooks::ClearUint,PostMutationHooks::OnClearUint);D18_ATTACH(PostMutationHooks::ClearFloat,PostMutationHooks::OnClearFloat);D18_ATTACH(PostMutationHooks::Resolve,PostMutationHooks::OnResolve);D18_ATTACH(PostMutationHooks::GenerateMips,PostMutationHooks::OnGenerateMips);}
        if(attach_setShader){D18_ATTACH(deferred_setShader,Deferred_setShader);}
        if(attach_draw){D18_ATTACH(deferred_draw,Deferred_draw);}
        if(attach_indexed){D18_ATTACH(deferred_indexed,Deferred_indexed);}
        if(attach_instanced){D18_ATTACH(deferred_instanced,Deferred_instanced);}
        if(attach_indexedInstanced){D18_ATTACH(deferred_indexedInstanced,Deferred_indexedInstanced);}
        if(attach_automatic){D18_ATTACH(deferred_automatic,Deferred_automatic);}
        if(attach_indirect){D18_ATTACH(deferred_indirect,Deferred_indirect);}
        if(attach_indexedIndirect){D18_ATTACH(deferred_indexedIndirect,Deferred_indexedIndirect);}
        D18_ATTACH(setCompute,OnSetCompute);D18_ATTACH(dispatch,OnDispatch);D18_ATTACH(dispatchIndirect,OnDispatchIndirect);
        if(attachCS){D18_ATTACH(deferredSetCompute,OnDeferredSetCompute);}if(attachDispatch){D18_ATTACH(deferredDispatch,OnDeferredDispatch);}if(attachDispatchIndirect){D18_ATTACH(deferredDispatchIndirect,OnDeferredDispatchIndirect);}
        #undef D18_ATTACH
        if(status==NO_ERROR)status=DetourTransactionCommit();else DetourTransactionAbort();
    }
    installed=status==NO_ERROR;
    if(installed&&DlssNr::WildlandsSr::postReplay){
        CommandLists::Register(9,cv[9]);CommandLists::Register(69,cv[69]);
        CommandLists::Register(12,cv[12]);
        CommandLists::Register(13,cv[13]);
        CommandLists::Register(14,cv[14]);
        CommandLists::Register(15,cv[15]);
        CommandLists::Register(20,cv[20]);
        CommandLists::Register(21,cv[21]);
        CommandLists::Register(38,cv[38]);
        CommandLists::Register(39,cv[39]);
        CommandLists::Register(40,cv[40]);
        CommandLists::Register(41,cv[41]);
        CommandLists::Register(42,cv[42]);
        CommandLists::Register(46,cv[46]);
        CommandLists::Register(47,cv[47]);
        CommandLists::Register(48,cv[48]);
        CommandLists::Register(50,cv[50]);
        CommandLists::Register(51,cv[51]);
        CommandLists::Register(52,cv[52]);
        CommandLists::Register(54,cv[54]);
        CommandLists::Register(57,cv[57]);
        CommandLists::Register(58,cv[58]);
        const auto commandStatus=CommandLists::Install(device);
        LogInput("{\"event\":\"command_list_hook_install\",\"status\":%ld,\"covered_writer_methods\":45}\n",commandStatus);FlushInput();
    }
    // The isolated fixture has no interactive window; never auto-arm in a game.
    if(installed&&!_wcsicmp(name.c_str(),L"d18-input-probe-host.exe")){sessions=1;armedUntil=GetFileAttributesW((root/L"D18InputProbe.unarmed-test").c_str())==INVALID_FILE_ATTRIBUTES?GetTickCount64()+15000:0;}
    LogInput("{\"event\":\"input_probe_install\",\"schema\":\"d18-input-observation-v2\",\"hook_status\":%ld,\"feature_level\":%u,\"trigger\":\"F8\",\"coverage\":\"early identity stages reported by input_creation_policy; swapchain-selected immediate/deferred PS and CS; upstream binding; CS UAV slots 0-7; GS stream-output creation excluded\",\"max_sessions\":3,\"max_samples_per_session\":864,\"deferred_context_available\":%s}\n",status,unsigned(device->GetFeatureLevel()),SUCCEEDED(deferredStatus)?"true":"false");if(shaderArchive)fflush(shaderArchive);FlushInput();
}
inline void Uninstall(){
    std::lock_guard lock(guard);
    if(CommandLists::armed.load()&&CommandLists::Detach()!=NO_ERROR)return;
    if(!installed){
        if(!probeReady)return;
        LONG status=DetourTransactionBegin();
        if(status==NO_ERROR){status=DetourUpdateThread(GetCurrentThread());if(status==NO_ERROR)status=DetachCreation();if(status==NO_ERROR)status=DetourTransactionCommit();else DetourTransactionAbort();}
        if(status==NO_ERROR){ResetCreation();CreationStats();Numeric::Stats();Numeric::Cleanup();if(output)fclose(output);output=nullptr;probeReady=false;if(shaderArchive){fclose(shaderArchive);shaderArchive=nullptr;}}
        return;
    }
    LONG status=DetourTransactionBegin();if(status!=NO_ERROR)return;
    status=DetourUpdateThread(GetCurrentThread());
    #define D18_DETACH(p,h) if(status==NO_ERROR)status=DetourDetach(reinterpret_cast<PVOID*>(&(p)),h)
    D18_DETACH(setShader,OnSetShader);D18_DETACH(draw,OnDraw);D18_DETACH(indexed,OnIndexed);D18_DETACH(instanced,OnInstanced);D18_DETACH(indexedInstanced,OnIndexedInstanced);D18_DETACH(automatic,OnAuto);D18_DETACH(indirect,OnIndirect);D18_DETACH(indexedIndirect,OnIndexedIndirect);
    if(Wildlands::enabled){D18_DETACH(Wildlands::map,Wildlands::OnMap);D18_DETACH(Wildlands::unmap,Wildlands::OnUnmap);D18_DETACH(Wildlands::update,Wildlands::OnUpdate);}
    D18_DETACH(execute,OnExecute);
    if(DlssNr::WildlandsSr::postReplay){D18_DETACH(PostMutationHooks::CopyResource,PostMutationHooks::OnCopyResource);D18_DETACH(PostMutationHooks::CopySubresource,PostMutationHooks::OnCopySubresource);D18_DETACH(PostMutationHooks::ClearTarget,PostMutationHooks::OnClearTarget);D18_DETACH(PostMutationHooks::ClearUint,PostMutationHooks::OnClearUint);D18_DETACH(PostMutationHooks::ClearFloat,PostMutationHooks::OnClearFloat);D18_DETACH(PostMutationHooks::Resolve,PostMutationHooks::OnResolve);D18_DETACH(PostMutationHooks::GenerateMips,PostMutationHooks::OnGenerateMips);}
    if(attach_setShader){D18_DETACH(deferred_setShader,Deferred_setShader);}
    if(attach_draw){D18_DETACH(deferred_draw,Deferred_draw);}
    if(attach_indexed){D18_DETACH(deferred_indexed,Deferred_indexed);}
    if(attach_instanced){D18_DETACH(deferred_instanced,Deferred_instanced);}
    if(attach_indexedInstanced){D18_DETACH(deferred_indexedInstanced,Deferred_indexedInstanced);}
    if(attach_automatic){D18_DETACH(deferred_automatic,Deferred_automatic);}
    if(attach_indirect){D18_DETACH(deferred_indirect,Deferred_indirect);}
    if(attach_indexedIndirect){D18_DETACH(deferred_indexedIndirect,Deferred_indexedIndirect);}
    D18_DETACH(setCompute,OnSetCompute);D18_DETACH(dispatch,OnDispatch);D18_DETACH(dispatchIndirect,OnDispatchIndirect);
    if(attachCS){D18_DETACH(deferredSetCompute,OnDeferredSetCompute);}if(attachDispatch){D18_DETACH(deferredDispatch,OnDeferredDispatch);}if(attachDispatchIndirect){D18_DETACH(deferredDispatchIndirect,OnDeferredDispatchIndirect);}
    if(status==NO_ERROR)status=DetachCreation();
    #undef D18_DETACH
    if(status==NO_ERROR)status=DetourTransactionCommit();else DetourTransactionAbort();
    if(status==NO_ERROR){ResetCreation();frameCounting.store(false);sampleOpen.store(false,std::memory_order_relaxed);Numeric::Stats();Numeric::Cleanup();if(selectedContext){selectedContext->Release();selectedContext=nullptr;}installed=false;probeReady=false;if(output){fclose(output);output=nullptr;}if(shaderArchive){fclose(shaderArchive);shaderArchive=nullptr;}if(observedDevice){observedDevice->Release();observedDevice=nullptr;}}
}
}


