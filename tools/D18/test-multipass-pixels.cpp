// Focused execution of the deployed high-resolution bytecode; no game or model.
#define NOMINMAX
#define wmain baseline_test_main
#include "test-nr-output-pixels.cpp"
#undef wmain
#include <dlssnr/HighResolutionNr.h>
#include <shaders/dlssnr/precompile/DlssNr_Multipass_Shader.h>
#include <functional>
using Pattern=std::function<Pixel(UINT,UINT)>;
void highPass(ID3D12Device* dev,ID3D12RootSignature* root,ID3D12PipelineState* pso,
             const DlssNrConstants& c,UINT sw,UINT sh,Pattern source,Pattern model,Pattern residual,Pattern expected,Pattern base=original){
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
    for(UINT y=0;y<c.Height;++y)for(UINT x=0;x<c.Width;++x){auto p=expected(x,y);auto v=reinterpret_cast<float*>(mem+y*fp.Footprint.RowPitch+x*sizeof(Pixel));
        for(int ch=0;ch<4;++ch) if(!std::isfinite(v[ch]) || std::abs(v[ch]-p[ch])>0.00004f){std::printf("mode=%u at %u,%u c%d got %.8f expected %.8f\n",c.Mode,x,y,ch,v[ch],p[ch]);throw std::runtime_error("high-resolution pixels");}}
    rb->Unmap(0,&empty);std::printf("PASS mode %u, %ux%u <- %ux%u, rect %u,%u %ux%u\n",c.Mode,c.Width,c.Height,sw,sh,c.ValidX,c.ValidY,c.ValidWidth,c.ValidHeight);
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
    DlssNrConstants c{};c.RelativeColour=2;c.WhitePoint=1;c.MaxRatio=4;c.DebugScale=1;c.NetworkRatioX=c.NetworkRatioY=1;
    c.Width=28;c.Height=23;c.ValidX=3;c.ValidY=2;c.ValidWidth=21;c.ValidHeight=17;
    auto zero=[](UINT,UINT){return Pixel{0,0,0,0};};
    auto p=[](UINT x,UINT y){return Pixel{(x%2)? .7f:.1f,(y%2)? .8f:.2f,.5f,1};};
    auto editedModel=[&](UINT x,UINT y){auto a=p(x,y);a[0]+=.025f;a[1]-=.03f;return a;};
    auto old=[](UINT,UINT){return Pixel{.02f,-.01f,.04f,0};};
    auto inside=[&](UINT x,UINT y){return x>=c.ValidX && y>=c.ValidY && x-c.ValidX<c.ValidWidth && y-c.ValidY<c.ValidHeight;};
    auto decode=[](double v){return v<.04045?v/12.92:pow((v+.055)/1.055,2.4);};
    for(UINT passthrough:{0u,1u}){
        c.Passthrough=passthrough;
        for(UINT mode:{7u,8u}){
            c.Mode=mode;
            highPass(dev.Get(),root.Get(),pso.Get(),c,28,23,p,p,zero,zero);
            auto expected=[&](UINT x,UINT y){Pixel d{};if(!inside(x,y))return d;auto a=p(x,y),b=editedModel(x,y),prev=old(x,y);
                for(int ch=0;ch<3;++ch)d[ch]=float(passthrough?b[ch]-a[ch]:decode(b[ch])-decode(a[ch]))+(mode==8?prev[ch]:0);return d;};
            highPass(dev.Get(),root.Get(),pso.Get(),c,28,23,p,editedModel,old,expected);
        }
    }
    c.Mode=9;highPass(dev.Get(),root.Get(),pso.Get(),c,28,23,p,p,old,zero);
    // Identity models must preserve SR even when both model inputs were strongly prefiltered.
    // Naively subtracting the final model answer from P0 would destroy this checkerboard.
    auto filtered=[](UINT,UINT){return Pixel{.4f,.5f,.5f,1};};
    c.Mode=8;c.Passthrough=0;
    highPass(dev.Get(),root.Get(),pso.Get(),c,28,23,filtered,filtered,zero,zero);
    c.Mode=1;c.TransferStrength=.75f;c.ColourStrength=.75f;
    highPass(dev.Get(),root.Get(),pso.Get(),c,28,23,p,p,zero,p,p);
    c.TransferStrength=0;c.ColourStrength=0;
    highPass(dev.Get(),root.Get(),pso.Get(),c,28,23,p,p,old,p,p);
    if(dbg){ComPtr<ID3D12InfoQueue> info;check(dev.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n=0;check(info->GetMessage(i,nullptr,&n));std::vector<char> b(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(b.data());check(info->GetMessage(i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(m->pDescription);throw std::runtime_error("debug layer");}}}
    std::printf("PASS multipass prototype residual and final-resolve bytecode, debug=%d model_loaded=false\n",dbg);return 0;
}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}
