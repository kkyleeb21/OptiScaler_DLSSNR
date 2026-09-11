// Executes the current production resolve shader on D3D12 WARP. No NGX or game.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <dlssnr/Dx12OutputContract.h>
#include <dlssnr/CaptureEvidence.h>
#include <dlssnr/DlssNr_Capture.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void check(HRESULT hr){if(FAILED(hr)){std::printf("HRESULT 0x%08X\n",static_cast<unsigned>(hr));throw std::runtime_error("D3D12 call failed");}}
D3D12_RESOURCE_DESC texture(UINT w,UINT h,bool uav){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
    d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    d.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;return d;
}
ComPtr<ID3D12Resource> create(ID3D12Device* dev,const D3D12_RESOURCE_DESC& d,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> r;
    check(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;
}
ComPtr<ID3D12Resource> buffer(ID3D12Device* dev,UINT64 bytes,bool readback){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;
    d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return create(dev,d,readback?D3D12_HEAP_TYPE_READBACK:D3D12_HEAP_TYPE_UPLOAD,
                  readback?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_GENERIC_READ);
}
void barrier(ID3D12GraphicsCommandList* cmd,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};cmd->ResourceBarrier(1,&v);
}
D3D12_TEXTURE_COPY_LOCATION location(ID3D12Resource* r){D3D12_TEXTURE_COPY_LOCATION l{};l.pResource=r;return l;}
using Pixel=std::array<float,4>;
Pixel original(UINT x,UINT y){return {0.12f+float(x)*0.021f,0.24f+float(y)*0.017f,0.7f,0.31f+float(x%3)*0.1f};}
void run(ID3D12Device* dev,ID3D12RootSignature* root,ID3D12PipelineState* pso,
    UINT w,UINT h,DlssNrAbi::Rect rect,bool uav,bool success,bool debug,float ratio,bool compact=true,
    const wchar_t* captureDirectory=nullptr){
    const auto desc=texture(w,h,uav);
    const bool allowed=DlssNr::CheckDx12OutputContract(desc,rect)==nullptr && success;
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc;check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> cmd;check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&cmd)));
    auto output=create(dev,desc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto base=create(dev,texture(w,h,false),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    const UINT pw=compact?static_cast<UINT>(std::ceil(float(w)*ratio)):w;
    const UINT ph=compact?static_cast<UINT>(std::ceil(float(h)*ratio)):h;
    auto proxy=create(dev,texture(pw,ph,false),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<ComPtr<ID3D12Resource>> uploads;
    auto upload=[&](ID3D12Resource* dst,bool constant){
        const auto d=dst->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;
        dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto up=buffer(dev,bytes,false);
        unsigned char* mem=nullptr;D3D12_RANGE empty{};check(up->Map(0,&empty,reinterpret_cast<void**>(&mem)));
        for(UINT y=0;y<d.Height;++y)for(UINT x=0;x<d.Width;++x){
            const Pixel pixel=constant?Pixel{0.25f,0.25f,0.25f,1.f}:original(x,y);
            std::memcpy(mem+y*fp.Footprint.RowPitch+x*sizeof(Pixel),pixel.data(),sizeof(Pixel));
        }
        up->Unmap(0,nullptr);auto s=location(up.Get()),t=location(dst);
        s.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;s.PlacedFootprint=fp;cmd->CopyTextureRegion(&t,0,0,0,&s,nullptr);
        uploads.push_back(up);
    };
    upload(output.Get(),false);upload(base.Get(),false);upload(proxy.Get(),true);
    barrier(cmd.Get(),base.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barrier(cmd.Get(),proxy.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12Resource> constants;
    capture::FrameCapture captured;
    capture::FrameEvidence captureMetadata{};captureMetadata.inputWidth=w;captureMetadata.inputHeight=h;
    if(allowed){
        barrier(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=7;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));const auto increment=dev->GetDescriptorHandleIncrementSize(hd.Type);
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<5;++i){
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
            view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            dev->CreateShaderResourceView(i==2?base.Get():proxy.Get(),&view,cpu);cpu.ptr+=increment;
        }
        for(UINT i=0;i<2;++i){D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=desc.Format;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            dev->CreateUnorderedAccessView(output.Get(),nullptr,&view,cpu);cpu.ptr+=increment;}
        DlssNrConstants c{};c.Mode=DlssNrMode_Resolve;c.Width=w;c.Height=h;c.WhitePoint=1;c.Passthrough=1;
        c.ValidX=rect.x;c.ValidY=rect.y;c.ValidWidth=rect.width;c.ValidHeight=rect.height;
        c.NetworkRatioX=ratio;c.NetworkRatioY=ratio;c.Transfer=1;c.MaxRatio=4;c.DebugView=debug?1:0;c.DebugScale=1;
        c.PreserveHighFrequency=1;c.ExperimentalCompose=1;c.GuidedReconstruction=1;c.LumaTrust=1;c.ChromaTrust=1;
        c.GuideWidth=pw;c.GuideHeight=ph;c.MvScaleX=1;c.MvScaleY=1;
        captureMetadata.resolve=c;
        constants=buffer(dev,sizeof(c),false);void* mapped=nullptr;D3D12_RANGE empty{};
        check(constants->Map(0,&empty,&mapped));std::memcpy(mapped,&c,sizeof(c));constants->Unmap(0,nullptr);
        ID3D12DescriptorHeap* heaps[]={heap.Get()};cmd->SetDescriptorHeaps(1,heaps);
        cmd->SetComputeRootSignature(root);cmd->SetPipelineState(pso);
        cmd->SetComputeRootConstantBufferView(0,constants->GetGPUVirtualAddress());
        cmd->SetComputeRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());
        cmd->Dispatch((w+7)/8,(h+7)/8,1);
        barrier(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    }else barrier(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    if(captureDirectory && allowed){
        captured.request(1);captureMetadata.frame=100;captureMetadata.successfulSinceReset=40;
        captureMetadata.rects.output=rect;captureMetadata.rects.depth={0,0,pw,ph};captureMetadata.rects.motion={0,0,pw,ph};
        captureMetadata.networkWidth=static_cast<UINT>(std::ceil(float(w)*ratio));
        captureMetadata.networkHeight=static_cast<UINT>(std::ceil(float(h)*ratio));
        captured.record(cmd.Get(),dev,base.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,proxy.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            proxy.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&captureMetadata);
        require(captured.readyToWrite(),"capture not recorded");
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;
    dev->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);auto readback=buffer(dev,bytes,true);
    auto from=location(output.Get()),to=location(readback.Get());to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
    cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);check(cmd->Close());
    ID3D12CommandList* lists[]={cmd.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE completed=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(completed!=nullptr,"event creation failed");
    check(queue->Signal(fence.Get(),1));check(fence->SetEventOnCompletion(1,completed));
    const auto waited=WaitForSingleObject(completed,30000);CloseHandle(completed);
    require(waited==WAIT_OBJECT_0,"WARP submission timeout");require(fence->GetCompletedValue()!=UINT64_MAX,"device removed");
    if(captureDirectory && allowed)require(!captured.write(captureDirectory).empty(),"GPU capture write failed");
    unsigned char* mapped=nullptr;D3D12_RANGE read{0,static_cast<SIZE_T>(bytes)};
    check(readback->Map(0,&read,reinterpret_cast<void**>(&mapped)));
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){
        const auto* got=reinterpret_cast<const float*>(mapped+y*fp.Footprint.RowPitch+x*sizeof(Pixel));
        auto expected=original(x,y);const bool inside=x>=rect.x && y>=rect.y && x-rect.x<rect.width && y-rect.y<rect.height;
        if(allowed && debug && inside)expected[0]=expected[1]=expected[2]=0.25f;
        for(UINT c=0;c<4;++c)require(std::isfinite(got[c]) && std::abs(got[c]-expected[c])<0.00002f,"pixel/alpha/padding differs");
    }
    D3D12_RANGE empty{};readback->Unmap(0,&empty);
    std::printf("PASS pixels: %ux%u active=%u,%u,%u,%u UAV=%d success=%d debug=%d ratio=%.2f proxy=%ux%u dispatched=%d\n",w,h,rect.x,rect.y,rect.width,rect.height,uav,success,debug,ratio,pw,ph,allowed);
}
int wmain(int argc,wchar_t** argv)try{
    require(argc==2 || argc==3,"pass the current production dlssnr.hlsl path and optional metadata output");
    auto d=texture(17,13,true);require(!DlssNr::CheckDx12OutputContract(d,{3,2,11,9}),"valid contract rejected");
    for(auto r:{DlssNrAbi::Rect{0,0,0,13},{0,0,18,13},{UINT32_MAX,0,3,2},{2,12,2,2}})
        require(DlssNr::CheckDx12OutputContract(d,r)!=nullptr,"invalid rectangle accepted");
    d.SampleDesc.Count=4;require(DlssNr::CheckDx12OutputContract(d,{0,0,17,13})!=nullptr,"MSAA accepted");
    d=texture(17,13,true);d.DepthOrArraySize=2;require(DlssNr::CheckDx12OutputContract(d,{0,0,17,13})!=nullptr,"array accepted");
    d=texture(17,13,true);d.Flags|=D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    require(DlssNr::CheckDx12OutputContract(d,{0,0,17,13})!=nullptr,"unreadable output accepted");
    // Independent guide extents, low/high resolution MV, and overflow-safe bounds.
    require(DlssNrAbi::Fits({2,3,1920,1080},1924,1084),"independent depth rejected");
    require(DlssNrAbi::Fits({5,7,1920,1080},1930,1090),"low-res MV rejected");
    require(DlssNrAbi::Fits({5,7,3840,2160},3850,2170),"high-res MV rejected");
    require(!DlssNrAbi::Fits({5,7,3840,2160},3840,2160),"guide overflow accepted");
    capture::HistoryEvidence history;history.observe(true,true);require(history.successful==1,"reset count");
    history.observe(false,true);require(history.successful==2,"history advance");history.observe(false,false);
    require(history.successful==0,"failure retained warmup");history.observe(false,true);history.observe(true,true);require(history.successful==1,"second reset");
    ComPtr<ID3D12Debug> debug;const bool debugEnabled=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if(debugEnabled)debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter;check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3DBlob> shader,errors;const auto compiled=D3DCompileFromFile(argv[1],nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "CSMain","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
    if(errors)std::printf("%s\n",static_cast<const char*>(errors->GetBufferPointer()));check(compiled);
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,5};
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[0].Descriptor={0,0};
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={2,ranges};
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rd{2,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> signature;
    check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors));ComPtr<ID3D12RootSignature> root;
    check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};ComPtr<ID3D12PipelineState> pso;
    check(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    for(float ratio:{0.5f,1.f})for(bool view:{false,true}){
        run(device.Get(),root.Get(),pso.Get(),17,13,{0,0,17,13},true,true,view,ratio);
        run(device.Get(),root.Get(),pso.Get(),17,13,{3,2,11,9},true,true,view,ratio);
    }
    run(device.Get(),root.Get(),pso.Get(),17,13,{3,2,11,9},false,true,true,0.5f);
    run(device.Get(),root.Get(),pso.Get(),17,13,{3,2,11,9},true,false,true,0.5f);
    run(device.Get(),root.Get(),pso.Get(),17,13,{3,2,17,13},true,true,true,0.5f);
    const auto capturePath=argc==3?(std::filesystem::path(argv[2]).parent_path()/L"gpu-capture").wstring():std::wstring{};
    run(device.Get(),root.Get(),pso.Get(),17,13,{3,2,11,9},true,true,false,0.5f,false,capturePath.empty()?nullptr:capturePath.c_str());
    run(device.Get(),root.Get(),pso.Get(),17,13,{3,2,11,9},true,true,true,0.5f,false);
    if(debugEnabled){ComPtr<ID3D12InfoQueue> info;check(device.As(&info));
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;check(info->GetMessage(i,nullptr,&size));std::vector<unsigned char> bytes(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());check(info->GetMessage(i,message,&size));
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::printf("DEBUG ERROR: %s\n",message->pDescription);throw std::runtime_error("debug validation failed");}
        }
    }
    if(argc==3){
        capture::FrameEvidence e{};e.frame=100;e.successfulSinceReset=40;e.networkWidth=9;e.networkHeight=7;e.inputWidth=14;e.inputHeight=11;
        e.rects.output={3,2,11,9};e.rects.depth={1,1,8,6};e.rects.motion={2,1,8,6};
        e.resolve.WhitePoint=2.5f;e.resolve.TransferStrength=0.5f;e.resolve.MvScaleX=1;e.resolve.MvScaleY=1;
        std::FILE* file=nullptr;require(_wfopen_s(&file,argv[2],L"wb")==0 && file!=nullptr,"metadata output unavailable");
        capture::writeEvidence(file,e);require(std::fclose(file)==0,"metadata close failed");
    }
    std::printf("PASS: 13 WARP readback cases; output/guide contracts, history evidence; debug_layer=%d\n",debugEnabled);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
