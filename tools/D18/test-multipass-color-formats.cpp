// Executes the production orchestration include with an identity/failure model.
// Shader and barriers are real D3D12; real NGX is covered by the runtime fixture.
#define NOMINMAX
#define wmain baseline_test_main
#include "test-nr-output-pixels.cpp"
#undef wmain
#include <dlssnr/MultipassPolicy.h>
#include <shaders/dlssnr/precompile/DlssNr_Multipass_Shader.h>
#include <functional>
#include <DirectXPackedVector.h>
#include <dlssnr/MultipassFormats.h>
#include <dlssnr/NrMemoryPolicy.h>
#include <memory>
#include <dlssnr/LayerCapture.h>
capture::LayerCapture g_layerCapture;
struct InitTicket {
    ComPtr<ID3D12Fence> fence;bool submitted=false;
    bool Complete()const {return submitted && fence && fence->GetCompletedValue()!=UINT64_MAX && fence->GetCompletedValue()>=1;}
};
using InitToken=std::shared_ptr<InitTicket>;
struct Setting {bool value;bool value_or_default()const{return value;}};
struct Config {Setting DlssNrCustomColorFilter{true},DlssNrCatmullRomInput{false};std::array<DlssNr::Multipass::Tuning,4> tuning;};
namespace DlssNr::Multipass {Tuning Read(const Config& c,unsigned i){return c.tuning[i];}}
struct NrScopeExit {std::function<void()> f;~NrScopeExit(){f();}};
void Barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){barrier(l,r,a,b);}
constexpr int NVSDK_NGX_Result_Success=1;
using Eval=std::function<int(ID3D12GraphicsCommandList*,void*,void*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,unsigned,unsigned,unsigned,unsigned,int,int,float,int,float,float,float,int,float,float,float,const DlssNrAbi::Frame*)>;
struct Extra {void* feature=nullptr;void* params=nullptr;DlssNr::Multipass::Tuning built;InitToken lastUse;bool reset=true;unsigned width=0;};
struct Multi {std::array<Extra,3> passes;ID3D12Resource* input=nullptr;ID3D12Resource* answer=nullptr;ID3D12Resource* zeroMotion=nullptr;ID3D12Resource* delta[2]{};unsigned ready=4,recorded=1;bool shared=false;InitToken scratchUse;DlssNr::CompletedInitialization<InitToken> zeroInitialized;const char* reason="";} g_multi;
struct NR {ID3D12Resource* output=nullptr;ID3D12Resource* colorCopy=nullptr;void* feature=nullptr;void* capabilityParams=nullptr;bool guideDepthInverted=false,failed=false,reset=false;float guideMvScaleX=32,guideMvScaleY=24,builtIntensity=1;const char* reason="";Eval evaluate;} g_nr;
bool g_deviceLost=false;
void ResetAdditional(){for(auto& p:g_multi.passes)p.reset=true;}
void MultipassEvent(const Config&,const char*,unsigned,uint32_t,const char*,bool=false,bool=false){}
unsigned colorBytes(DXGI_FORMAT f){return f==DXGI_FORMAT_R32G32B32A32_FLOAT?16:(f==DXGI_FORMAT_R16G16B16A16_FLOAT||f==DXGI_FORMAT_R16G16B16A16_UNORM)?8:4;}
void storeColor(unsigned char* dst,const Pixel& v,DXGI_FORMAT f){
 if(f==DXGI_FORMAT_R32G32B32A32_FLOAT){memcpy(dst,v.data(),16);return;}
 if(f==DXGI_FORMAT_R11G11B10_FLOAT){DirectX::PackedVector::XMFLOAT3PK p(v[0],v[1],v[2]);memcpy(dst,&p,4);return;}
 if(f==DXGI_FORMAT_R16G16B16A16_FLOAT||f==DXGI_FORMAT_R16G16B16A16_UNORM){unsigned short p[4];for(int i=0;i<4;++i)p[i]=f==DXGI_FORMAT_R16G16B16A16_FLOAT?DirectX::PackedVector::XMConvertFloatToHalf(v[i]):static_cast<unsigned short>(std::round(v[i]*65535));memcpy(dst,p,8);return;}
 if(f==DXGI_FORMAT_R10G10B10A2_UNORM){unsigned p=unsigned(std::round(v[0]*1023))|(unsigned(std::round(v[1]*1023))<<10)|(unsigned(std::round(v[2]*1023))<<20)|(unsigned(std::round(v[3]*3))<<30);memcpy(dst,&p,4);return;}
 for(int i=0;i<4;++i)dst[i]=static_cast<unsigned char>(std::round(v[f==DXGI_FORMAT_B8G8R8A8_UNORM&&i<3?2-i:i]*255));
}
void chain(DXGI_FORMAT external,ID3D12Device* device,ID3D12RootSignature* root,ID3D12PipelineState* pso,unsigned count,bool shared,bool custom,int failPass,DXGI_FORMAT motionFormat=DXGI_FORMAT_R32G32_FLOAT){
    constexpr UINT width=64,height=48,guideWidth=32,guideHeight=24;
    g_multi=Multi{};g_nr=NR{};g_multi.ready=count;g_multi.shared=shared;Config cfg;cfg.DlssNrCustomColorFilter.value=custom;
    for(unsigned i=0;i<4;++i){cfg.tuning[i].ratio=shared?.5f:(.5f+float(i)/6);if(i)g_multi.passes[i-1].built=cfg.tuning[i];}
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));auto* cmdList=list.Get();
    std::vector<ComPtr<ID3D12Resource>> retained;
    auto upload=[&](bool filtered,D3D12_RESOURCE_STATES state,DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN){if(format==DXGI_FORMAT_UNKNOWN)format=external;auto d=texture(width,height,true);d.Format=format;auto dst=create(device,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto up=buffer(device,bytes,false);unsigned char* mem=nullptr;D3D12_RANGE empty{};check(up->Map(0,&empty,reinterpret_cast<void**>(&mem)));
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){Pixel v=filtered?Pixel{.25f,.5f,.75f,1}:Pixel{x%2?.75f:.125f,y%2?.5f:.25f,.75f,.5f};storeColor(mem+y*fp.Footprint.RowPitch+x*colorBytes(format),v,format);}

        up->Unmap(0,nullptr);auto from=location(up.Get()),to=location(dst.Get());from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;cmdList->CopyTextureRegion(&to,0,0,0,&from,nullptr);Barrier(cmdList,dst.Get(),D3D12_RESOURCE_STATE_COPY_DEST,state);retained.push_back(up);retained.push_back(dst);return dst.Get();};
    const auto srv=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,uav=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    auto* target=upload(false,uav);auto* originalBase=upload(false,srv);g_nr.colorCopy=upload(false,srv,DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto* modelInput=upload(true,srv,DXGI_FORMAT_R16G16B16A16_FLOAT);g_nr.output=upload(true,srv,DXGI_FORMAT_R16G16B16A16_FLOAT);g_multi.input=upload(true,uav,DXGI_FORMAT_R16G16B16A16_FLOAT);g_multi.answer=upload(true,uav,DXGI_FORMAT_R16G16B16A16_FLOAT);
    g_multi.delta[0]=upload(true,uav,DXGI_FORMAT_R16G16B16A16_FLOAT);g_multi.delta[1]=upload(true,uav,DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto motionDesc=texture(guideWidth,guideHeight,true);motionDesc.Format=motionFormat;
    auto zeroMotion=create(device,motionDesc,D3D12_HEAP_TYPE_DEFAULT,uav);g_multi.zeroMotion=zeroMotion.Get();
    auto* depthIn=modelInput;auto* motionIn=modelInput;
    std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
    unsigned dispatches=0,evals=0,finalCompositions=0;bool modelCopy=false;
    auto DispatchPass=[&](ID3D12GraphicsCommandList* listArg,const DlssNrConstants& c,ID3D12Resource* source,ID3D12Resource* model,ID3D12Resource* base,ID3D12Resource* motion,ID3D12Resource* residual,ID3D12Resource* output,ID3D12Resource*){
        if(!modelCopy)++dispatches;if(c.Mode==1)++finalCompositions;
        ComPtr<ID3D12DescriptorHeap> heap;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=7;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart();auto stride=device->GetDescriptorHandleIncrementSize(hd.Type);
        for(auto* input:{source,model?model:source,base?base:source,motion?motion:source,residual?residual:source}){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=input->GetDesc().Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;device->CreateShaderResourceView(input,&v,cpu);cpu.ptr+=stride;}
        for(unsigned i=0;i<2;++i){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=output->GetDesc().Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(output,nullptr,&v,cpu);cpu.ptr+=stride;}
        auto cb=buffer(device,sizeof(c),false);void* mapped=nullptr;D3D12_RANGE empty{};check(cb->Map(0,&empty,&mapped));memcpy(mapped,&c,sizeof(c));cb->Unmap(0,nullptr);
        ID3D12DescriptorHeap* hs[]={heap.Get()};listArg->SetDescriptorHeaps(1,hs);listArg->SetComputeRootSignature(root);listArg->SetPipelineState(pso);listArg->SetComputeRootConstantBufferView(0,cb->GetGPUVirtualAddress());listArg->SetComputeRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());
        const unsigned dw=c.Mode==DlssNrMode_ColorPrefilter?c.SourceWidth:c.Width,dh=c.Mode==DlssNrMode_ColorPrefilter?c.SourceHeight:c.Height;
        listArg->Dispatch((dw+7)/8,(dh+7)/8,1);heaps.push_back(heap);retained.push_back(cb);return true;};
    g_nr.evaluate=[&](ID3D12GraphicsCommandList* l,void*,void*,ID3D12Resource* input,ID3D12Resource*,ID3D12Resource* mv,ID3D12Resource* output,unsigned,unsigned,unsigned,unsigned,int,int reset,float,int,float,float,float,int,float,float,float,const DlssNrAbi::Frame*){
        ++evals;require(!shared||(!reset&&mv==zeroMotion.Get()),"shared reset and motion contract");
        if(int(evals)+1==failPass)return -1;
        DlssNrConstants copy{};copy.Mode=DlssNrMode_ColorPrefilter;copy.Width=copy.SourceWidth=width;copy.Height=copy.SourceHeight=height;copy.NetworkRatioX=copy.NetworkRatioY=1;modelCopy=true;DispatchPass(l,copy,input,nullptr,nullptr,nullptr,nullptr,output,nullptr);modelCopy=false;return 1;};
    DlssNrAbi::Frame modelRects;modelRects.color=modelRects.output={0,0,width,height};modelRects.depth=modelRects.motion={0,0,guideWidth,guideHeight};
    struct {DlssNrAbi::Frame Rects;} frame{modelRects};auto submission=std::make_shared<InitTicket>();
    DlssNrConstants encodeParams{};encodeParams.Width=width;encodeParams.Height=height;encodeParams.WhitePoint=1;encodeParams.Passthrough=0;
    DlssNrConstants resolveParams=encodeParams;resolveParams.Mode=1;resolveParams.ValidWidth=width;resolveParams.ValidHeight=height;resolveParams.TransferStrength=1;resolveParams.ColourStrength=1;resolveParams.MaxRatio=4;resolveParams.PreserveHighFrequency=1;resolveParams.NetworkRatioX=resolveParams.NetworkRatioY=.5f;
    bool residualReady=true;
    {
        ID3D12Resource* multipassDelta=nullptr;
#include <shaders/dlssnr/Multipass_Execute_Dx12.inl>
        if(residualReady)require(DispatchPass(cmdList,resolveParams,g_nr.colorCopy,g_nr.colorCopy,originalBase,motionIn,multipassDelta,target,nullptr),"final compose");
        Barrier(cmdList,g_nr.output,srv,uav);
    }
    require(evals==(failPass?unsigned(failPass-1):count-1),"all requested model calls");require(finalCompositions==(failPass?0u:1u),"one final composition or untouched fallback");
    require(dispatches<=DlssNr::Multipass::Slots(count)+(shared?1:0),"descriptor admission bound");
    auto d=target->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto rb=buffer(device,bytes,true);
    Barrier(cmdList,target,uav,D3D12_RESOURCE_STATE_COPY_SOURCE);auto from=location(target),to=location(rb.Get());to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;cmdList->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    check(cmdList->Close());ID3D12CommandList* lists[]={cmdList};queue->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence> fence;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));check(queue->Signal(fence.Get(),1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"event");check(fence->SetEventOnCompletion(1,event));require(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"GPU completion");CloseHandle(event);
    unsigned char* mem=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};check(rb->Map(0,&range,reinterpret_cast<void**>(&mem)));
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){unsigned char expected[16]{};storeColor(expected,Pixel{x%2?.75f:.125f,y%2?.5f:.25f,.75f,.5f},external);require(memcmp(mem+y*fp.Footprint.RowPitch+x*colorBytes(external),expected,colorBytes(external))==0,"SR identity/failure must remain bit-exact in its original format");}

    D3D12_RANGE empty{};rb->Unmap(0,&empty);printf("PASS production chain count=%u shared=%u custom=%u failed_pass=%d evals=%u final_compose=%u GPU_complete original_exact\n",count,shared,custom,failPass,evals,finalCompositions);
}
int wmain(int argc,wchar_t**)try{
    ComPtr<ID3D12Debug> debug;check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;if(argc>1){for(UINT i=0;factory->EnumAdapters(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC d{};adapter->GetDesc(&d);if(d.VendorId==0x10de)break;adapter.Reset();}require(adapter!=nullptr,"NVIDIA adapter");}else check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));ComPtr<ID3D12Device> dev;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
    D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,5}};D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={2,ranges};
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;D3D12_ROOT_SIGNATURE_DESC rd{2,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> sig,error;check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&error));ComPtr<ID3D12RootSignature> root;check(dev->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&root)));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={DlssNr_Multipass_cso,sizeof(DlssNr_Multipass_cso)};ComPtr<ID3D12PipelineState> pso;check(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    for(auto f:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM}){
      if(!DlssNr::Multipass::ColorFormatSupported(dev.Get(),f)){printf("SKIP unsupported device format %u\n",unsigned(f));continue;}
      printf("FORMAT %u: private FP16 model/residuals, original SR storage preserved\n",unsigned(f));
      for(unsigned count:{2u,3u,4u})for(bool shared:{false,true})for(bool custom:{false,true})chain(f,dev.Get(),root.Get(),pso.Get(),count,shared,custom,0);
      for(bool shared:{false,true})chain(f,dev.Get(),root.Get(),pso.Get(),4,shared,true,3);
    }
    ComPtr<ID3D12InfoQueue> info;check(dev.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<char> data(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());check(info->GetMessage(i,m,&size));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(m->pDescription);throw std::runtime_error("D3D12 validation error");}}
    puts("PASS format-matrix production orchestration, capture off, debug layer clean; identity/failure stub model, no game");return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
