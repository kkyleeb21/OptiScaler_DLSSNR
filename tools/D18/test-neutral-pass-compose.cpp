// Focused execution of the deployed high-resolution bytecode; no game or model.
#define NOMINMAX
#define wmain baseline_test_main
#include "test-nr-output-pixels.cpp"
#undef wmain
#include <dlssnr/HighResolutionNr.h>
#include <shaders/dlssnr/precompile/DlssNr_Multipass_Shader.h>
#include <functional>
#include <initializer_list>
#include <shaders/dlssnr/precompile/DlssNr_Shader.h>
#include <shaders/dlssnr/precompile/DlssNr_HighResolution_Shader.h>
using Pattern=std::function<Pixel(UINT,UINT)>;
std::vector<Pixel> highPass(ID3D12Device* dev,ID3D12RootSignature* root,ID3D12PipelineState* pso,
             const DlssNrConstants& c,UINT sw,UINT sh,Pattern source,Pattern model,Pattern residual,Pattern base=original){
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc;check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> cmd;check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&cmd)));
    std::vector<ComPtr<ID3D12Resource>> uploads, inputs;
    const auto upload=[&](UINT w,UINT h,Pattern pattern,bool writable){
        auto dst=create(dev,texture(w,h,writable),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        auto d=dst->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;
        dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto up=buffer(dev,bytes,false);
        unsigned char* mem=nullptr;D3D12_RANGE empty{};check(up->Map(0,&empty,reinterpret_cast<void**>(&mem)));
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto p=pattern(x,y);memcpy(mem+y*fp.Footprint.RowPitch+x*sizeof(Pixel),p.data(),sizeof(Pixel));}
        up->Unmap(0,nullptr);auto from=location(up.Get()),to=location(dst.Get());from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
        cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        barrier(cmd.Get(),dst.Get(),D3D12_RESOURCE_STATE_COPY_DEST,writable?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        uploads.push_back(up);return dst;
    };
    inputs.push_back(upload(sw,sh,source,false));inputs.push_back(upload(sw,sh,model,false));
    inputs.push_back(upload(c.Width,c.Height,base,false));inputs.push_back(inputs[0]);
    inputs.push_back(upload(c.Width,c.Height,residual,false));
    auto out=upload(c.Width,c.Height,base,true);auto keep=upload(c.Width,c.Height,base,true);
    ComPtr<ID3D12DescriptorHeap> heap;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors=7;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;check(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();const auto stride=dev->GetDescriptorHandleIncrementSize(hd.Type);
    for(auto& input:inputs){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;dev->CreateShaderResourceView(input.Get(),&v,cpu);cpu.ptr+=stride;}
    for(auto* dst:{out.Get(),keep.Get()}){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        dev->CreateUnorderedAccessView(dst,nullptr,&v,cpu);cpu.ptr+=stride;}
    auto cb=buffer(dev,sizeof(c),false);void* mapped=nullptr;D3D12_RANGE empty{};check(cb->Map(0,&empty,&mapped));memcpy(mapped,&c,sizeof(c));cb->Unmap(0,nullptr);
    ID3D12DescriptorHeap* heaps[]={heap.Get()};cmd->SetDescriptorHeaps(1,heaps);cmd->SetComputeRootSignature(root);cmd->SetPipelineState(pso);
    cmd->SetComputeRootConstantBufferView(0,cb->GetGPUVirtualAddress());cmd->SetComputeRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());cmd->Dispatch((c.Width+7)/8,(c.Height+7)/8,1);
    auto d=out->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto* read=c.Mode==0?keep.Get():out.Get();
    auto rb=buffer(dev,bytes,true);barrier(cmd.Get(),read,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    auto from=location(read),to=location(rb.Get());to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    check(cmd->Close());ID3D12CommandList* lists[]={cmd.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));check(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"event");check(fence->SetEventOnCompletion(1,event));
    auto wait=WaitForSingleObject(event,30000);CloseHandle(event);require(wait==WAIT_OBJECT_0 && fence->GetCompletedValue()!=UINT64_MAX,"completion");
    unsigned char* mem=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};check(rb->Map(0,&range,reinterpret_cast<void**>(&mem)));
    std::vector<Pixel> pixels(c.Width*c.Height);
    for(UINT y=0;y<c.Height;++y)for(UINT x=0;x<c.Width;++x){
        auto& p=pixels[y*c.Width+x];memcpy(p.data(),mem+y*fp.Footprint.RowPitch+x*sizeof(Pixel),sizeof(Pixel));
        for(float v:p)require(std::isfinite(v),"finite output");
    }
    rb->Unmap(0,&empty);return pixels;
}
int wmain(int argc,wchar_t**)try{
    ComPtr<ID3D12Debug> debug;const bool dbg=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));if(dbg)debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    if(argc>1){for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(d.VendorId==0x10de){std::wprintf(L"GPU %s\n",d.Description);break;}adapter.Reset();}require(adapter!=nullptr,"NVIDIA adapter");}
    else check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> dev;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
    D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,5}};
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={2,ranges};
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;
    D3D12_ROOT_SIGNATURE_DESC rd{2,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> signature,err;check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&err));
    ComPtr<ID3D12RootSignature> root;check(dev->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={DlssNr_Multipass_cso,sizeof(DlssNr_Multipass_cso)};ComPtr<ID3D12PipelineState> pso;check(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    pd.CS={DlssNr_cso,sizeof(DlssNr_cso)};ComPtr<ID3D12PipelineState> single;
    check(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&single)));
    pd.CS={DlssNr_HighResolution_cso,sizeof(DlssNr_HighResolution_cso)};ComPtr<ID3D12PipelineState> high;
    check(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&high)));
    auto decode=[](float v){v=std::clamp(v,0.f,1.f);return v<.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
    for(UINT advancedMode:{1u,2u})for(UINT passthrough:{0u,1u})for(float ratio:{1.f,.5f})for(bool overshoot:{false,true})for(UINT preserve:{0u,1u}){
        DlssNrConstants c{};c.Mode=1;c.Width=32;c.Height=24;c.ValidWidth=c.Width;c.ValidHeight=c.Height;
        c.WhitePoint=1;c.Passthrough=passthrough;c.MaxRatio=2;c.TransferStrength=1;c.ColourStrength=.5f;
        c.NetworkRatioX=c.NetworkRatioY=ratio;c.PreserveHighFrequency=preserve;c.Transfer=1;
        auto input=[](UINT x,UINT y){const float z=((x+y)%2)? .07f:0.f;return Pixel{.4f+z,.3f+z,.2f+z,1};};
        auto model=[&](UINT x,UINT y){auto v=input(x,y);v[0]=overshoot?1.2f:v[0]*1.1f;v[1]*=.85f;return v;};
        auto base=[&](UINT x,UINT y){auto v=input(x,y);if(!passthrough)for(int i=0;i<3;++i)v[i]=decode(v[i]);return v;};
        auto delta=[&](UINT x,UINT y){auto a=input(x,y),b=model(x,y);Pixel v{};
            for(int i=0;i<3;++i)v[i]=passthrough?b[i]-a[i]:decode(b[i])-decode(a[i]);return v;};
        auto a=highPass(dev.Get(),root.Get(),single.Get(),c,32,24,input,model,delta,base);
        c.RelativeColour=advancedMode;c.Transfer=0;c.ExperimentalCompose=0;c.GuidedReconstruction=0;c.PreserveHighFrequency=0;
        c.NetworkRatioX=c.NetworkRatioY=1;
#ifdef D18_ALL_MODE_HF
        c.PreserveHighFrequency=preserve;c.NetworkRatioX=c.NetworkRatioY=ratio;
#endif
        // Production advanced resolve binds the original proxy, not raw M2, in gModel.
        auto b=highPass(dev.Get(),root.Get(),advancedMode==2?pso.Get():high.Get(),c,32,24,input,input,delta,base);
        double maxError=0,meanError=0;unsigned changed=0;
        for(size_t i=0;i<a.size();++i)for(int ch=0;ch<3;++ch){double e=std::abs(a[i][ch]-b[i][ch]);maxError=std::max(maxError,e);meanError+=e;if(e>1e-5)++changed;}
        meanError/=a.size()*3;
        printf("advanced=%u passthrough=%u ratio=%.2f out_of_cube=%d preserve_hf=%u max_abs=%.9f mean_abs=%.9f changed_channels=%u total=%zu\n",advancedMode,passthrough,ratio,overshoot,preserve,maxError,meanError,changed,a.size()*3);
#ifdef D18_ALL_MODE_HF
        // Linear residual interpolation differs from interpolation-before-sRGB-decode.
        // Passthrough must agree; HDR equivalence is enforced when protection is off.
        if(passthrough || !preserve)require(maxError<.00004,"neutral second pass equivalence");
#else
        if((ratio==1 || !preserve) && (!overshoot || passthrough))require(maxError<.00004,"neutral second pass equivalence");
#endif
    }
    if(dbg){ComPtr<ID3D12InfoQueue> info;check(dev.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n=0;check(info->GetMessage(i,nullptr,&n));std::vector<char> b(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(b.data());check(info->GetMessage(i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)throw std::runtime_error(m->pDescription);}}
    puts("PASS neutral second pass diagnostic; synthetic nonidentity first model; FP32 isolates composition; real_model_loaded=false");return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
