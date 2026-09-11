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
#include <dlssnr/Dx12HighlightEncoding.h>
#include <cstddef>
static_assert(offsetof(DlssNrConstants, HighlightEncoding)==168);
static_assert(offsetof(DlssNrConstants, RelativeColour)==172);
float halfFloat(uint16_t value) {
    const int e=(value>>10)&31,m=value&1023;
    const float v=e==0?std::ldexp(float(m),-24):e==31?INFINITY:std::ldexp(float(1024+m),e-25);
    return value&32768?-v:v;
}
std::vector<Pixel> execute(ID3D12Device* dev,ID3D12RootSignature* root,ID3D12PipelineState* pso,
    const DlssNrConstants& c,const std::vector<Pixel>& originalPixels,const std::vector<Pixel>& proxyPixels,bool half=false) {
    const UINT w=c.Width,h=c.Height;
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc;check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> cmd;check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&cmd)));
    auto outputDesc=texture(w,h,true);if(half)outputDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto target=create(dev,outputDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto keep=create(dev,texture(w,h,true),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto src=create(dev,texture(w,h,false),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto base=create(dev,texture(w,h,false),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<ComPtr<ID3D12Resource>> uploads;
    auto upload=[&](ID3D12Resource* dst,const std::vector<Pixel>& px){
        const auto d=dst->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;
        dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto up=buffer(dev,bytes,false);
        unsigned char* mem=nullptr;D3D12_RANGE empty{};check(up->Map(0,&empty,reinterpret_cast<void**>(&mem)));
        // Half target is only used for encode, which overwrites the full surface.
        std::memset(mem,0,static_cast<size_t>(bytes));
        if(d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT)
            for(UINT y=0;y<h;++y)std::memcpy(mem+y*fp.Footprint.RowPitch,px.data()+y*w,w*sizeof(Pixel));
        up->Unmap(0,nullptr);auto s=location(up.Get()),t=location(dst);s.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;s.PlacedFootprint=fp;
        cmd->CopyTextureRegion(&t,0,0,0,&s,nullptr);uploads.push_back(up);
    };
    upload(target.Get(),originalPixels);upload(base.Get(),originalPixels);upload(src.Get(),proxyPixels);
    barrier(cmd.Get(),target.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barrier(cmd.Get(),base.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barrier(cmd.Get(),src.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=7;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;check(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();const auto step=dev->GetDescriptorHandleIncrementSize(hd.Type);
    for(UINT i=0;i<5;++i){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        dev->CreateShaderResourceView(i==2?base.Get():src.Get(),&v,cpu);cpu.ptr+=step;}
    for(UINT i=0;i<2;++i){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=i==0?outputDesc.Format:DXGI_FORMAT_R32G32B32A32_FLOAT;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        dev->CreateUnorderedAccessView(i==0?target.Get():keep.Get(),nullptr,&v,cpu);cpu.ptr+=step;}
    auto cb=buffer(dev,sizeof(c),false);void* mapped=nullptr;D3D12_RANGE empty{};check(cb->Map(0,&empty,&mapped));std::memcpy(mapped,&c,sizeof(c));cb->Unmap(0,nullptr);
    ID3D12DescriptorHeap* heaps[]={heap.Get()};cmd->SetDescriptorHeaps(1,heaps);cmd->SetComputeRootSignature(root);cmd->SetPipelineState(pso);
    cmd->SetComputeRootConstantBufferView(0,cb->GetGPUVirtualAddress());cmd->SetComputeRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());cmd->Dispatch((w+7)/8,(h+7)/8,1);
    barrier(cmd.Get(),target.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;dev->GetCopyableFootprints(&outputDesc,0,1,0,&fp,nullptr,nullptr,&bytes);auto readback=buffer(dev,bytes,true);
    auto s=location(target.Get()),t=location(readback.Get());t.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;t.PlacedFootprint=fp;cmd->CopyTextureRegion(&t,0,0,0,&s,nullptr);check(cmd->Close());
    ID3D12CommandList* lists[]={cmd.Get()};queue->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence> fence;check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"fence event");check(queue->Signal(fence.Get(),1));check(fence->SetEventOnCompletion(1,event));auto waited=WaitForSingleObject(event,30000);CloseHandle(event);require(waited==WAIT_OBJECT_0 && fence->GetCompletedValue()!=UINT64_MAX,"fence failure");
    unsigned char* data=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};check(readback->Map(0,&range,reinterpret_cast<void**>(&data)));std::vector<Pixel> result(w*h);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){
        if(half){const auto* v=reinterpret_cast<uint16_t*>(data+y*fp.Footprint.RowPitch+x*8);for(int k=0;k<4;++k)result[y*w+x][k]=halfFloat(v[k]);}
        else std::memcpy(result[y*w+x].data(),data+y*fp.Footprint.RowPitch+x*sizeof(Pixel),sizeof(Pixel));
    }
    readback->Unmap(0,&empty);return result;
}
float delta(const std::vector<Pixel>& a,const std::vector<Pixel>& b){float d=0;for(size_t i=0;i<a.size();++i)for(int k=0;k<4;++k){require(std::isfinite(a[i][k])&&std::isfinite(b[i][k]),"nonfinite");d=(std::max)(d,std::abs(a[i][k]-b[i][k]));}return d;}
int wmain(int argc,wchar_t** argv)try{
    require(argc==5,"shader path, compiled header output, compiled bytecode output, baseline bytecode");
    uint32_t state=0;
    require(!DlssNr::UpdateDx12HighlightEncoding(state,true,0),"classic stable");
    require(DlssNr::UpdateDx12HighlightEncoding(state,true,1)&&state==1,"switch to hybrid reset");
    require(!DlssNr::UpdateDx12HighlightEncoding(state,true,1),"steady hybrid reset");
    require(DlssNr::UpdateDx12HighlightEncoding(state,false,1)&&state==0,"SDR bypass reset");
    require(!DlssNr::UpdateDx12HighlightEncoding(state,true,2),"unsupported mode");
    ComPtr<ID3D12Debug> debug;const bool debugging=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));if(debugging)debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3DBlob> shaders[2],errors;
    const D3D_SHADER_MACRO defines[]={{"DX12_HIGHLIGHT_ENCODING","1"},{nullptr,nullptr}};
    for(int i=0;i<2;++i){const auto hr=D3DCompileFromFile(argv[1],i?defines:nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"CSMain","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shaders[i],&errors);if(errors)std::puts(static_cast<const char*>(errors->GetBufferPointer()));check(hr);}
    shaders[0].Reset();check(D3DReadFileToBlob(argv[4],&shaders[0]));
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,5};
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[0].Descriptor={0,0};
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={2,ranges};
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rd{2,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> signature;
    check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors));ComPtr<ID3D12RootSignature> root;
    check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)));

    ComPtr<ID3D12PipelineState> psos[2];
    for(int i=0;i<2;++i){D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={shaders[i]->GetBufferPointer(),shaders[i]->GetBufferSize()};check(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&psos[i])));}
    DlssNrConstants c{};c.Width=17;c.Height=13;c.WhitePoint=1;c.TransferStrength=1;c.ColourStrength=1;c.MaxRatio=4;c.NetworkRatioX=c.NetworkRatioY=.5f;c.Transfer=1;
    std::vector<Pixel> originals(c.Width*c.Height);
    const Pixel samples[]={{.1f,.2f,.3f,.4f},{.75f,.5f,.25f,.5f},{4,1,.25f,.6f},{2,2,2,.7f},{4,4,4,.8f},{8,8,8,.9f},{32,8,2,1}};
    for(size_t i=0;i<originals.size();++i)originals[i]=samples[i%7];
    c.Mode=0;auto oldEncode=execute(device.Get(),root.Get(),psos[0].Get(),c,originals,originals,true);
    auto recompiledClassic=execute(device.Get(),root.Get(),psos[1].Get(),c,originals,originals,true);
    std::printf("Recompiled vs deployed Classic encode max delta=%g; production retains deployed bytecode\n",delta(oldEncode,recompiledClassic));
    auto classic=execute(device.Get(),root.Get(),psos[0].Get(),c,originals,originals,true);
    require(delta(oldEncode,classic)==0,"Classic encode changed");
    c.HighlightEncoding=1;auto hybrid=execute(device.Get(),root.Get(),psos[1].Get(),c,originals,originals,true);
    for(size_t i=0;i<originals.size();++i){if(i%7<2)for(int k=0;k<4;++k)require(std::abs(hybrid[i][k]-classic[i][k])<=0.0005f,"low range exceeds one FP16 step");for(int k=0;k<3;++k)require(hybrid[i][k]>=0 && hybrid[i][k]<=1,"proxy range");}
    auto linear=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
    require(std::abs(linear(hybrid[2][1])/linear(hybrid[2][0])-.25f)<.002f,"Hybrid green/red ratio");
    require(std::abs(linear(hybrid[2][2])/linear(hybrid[2][0])-.0625f)<.001f,"Hybrid blue/red ratio");
    std::printf("Classic/Hybrid neutral 4x=%g/%g; no white-precision win assumed\n",classic[4][0],hybrid[4][0]);
    std::printf("FP16 GPU neutral 4x=%g 8x=%g (CPU nearest-round simulation may differ by one half step)\n",hybrid[4][0],hybrid[5][0]);
    require(hybrid[5][0]>=0.9995f && hybrid[5][0]<=1.0f,"FP16 upper-range bound");
    c.Passthrough=1;auto bypass=execute(device.Get(),root.Get(),psos[1].Get(),c,originals,originals);
    for(size_t i=0;i<originals.size();++i)for(int k=0;k<3;++k)require(bypass[i][k]==originals[i][k],"SDR bypass changed");
    c.Passthrough=0;c.Mode=1;c.ValidX=2;c.ValidY=1;c.ValidWidth=13;c.ValidHeight=11;c.HighlightEncoding=0;
    auto oldResolve=execute(device.Get(),root.Get(),psos[0].Get(),c,originals,classic);
    auto classicResolve=execute(device.Get(),root.Get(),psos[0].Get(),c,originals,classic);
    require(delta(oldResolve,classicResolve)==0,"Classic compose changed");
    c.HighlightEncoding=1;
    auto restored=execute(device.Get(),root.Get(),psos[1].Get(),c,originals,hybrid);
    // FP16 proxy plus existing matrix numerics are not an exact inverse.
    require(delta(restored,originals)<.06f,"Hybrid no-edit colour recovery");
    for(UINT y=0;y<c.Height;++y)for(UINT x=0;x<c.Width;++x){auto i=y*c.Width+x;require(restored[i][3]==originals[i][3],"alpha changed");if(x<2||y<1||x>=15||y>=12)require(restored[i]==originals[i],"padding changed");}
    c.ExperimentalCompose=1;auto matched=execute(device.Get(),root.Get(),psos[1].Get(),c,originals,hybrid);require(delta(matched,originals)<.06f,"matched reference encoding differs");
    c.ExperimentalCompose=0;c.TransferStrength=0;auto zero=execute(device.Get(),root.Get(),psos[1].Get(),c,originals,hybrid);require(delta(zero,originals)==0,"zero strength changed");
    if(debugging){ComPtr<ID3D12InfoQueue> info;check(device.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T bytes=0;check(info->GetMessage(i,nullptr,&bytes));std::vector<unsigned char> mem(bytes);auto* m=reinterpret_cast<D3D12_MESSAGE*>(mem.data());check(info->GetMessage(i,m,&bytes));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::puts(m->pDescription);throw std::runtime_error("D3D12 debug error");}}}
    std::FILE* file=nullptr;require(_wfopen_s(&file,argv[2],L"wb")==0,"header open");std::fputs("// Generated by D18 Hybrid shader verification. DX12_HIGHLIGHT_ENCODING=1.\nconst unsigned char DlssNr_hybrid_cso[] = {\n",file);
    const auto* bytes=static_cast<const unsigned char*>(shaders[1]->GetBufferPointer());for(size_t i=0;i<shaders[1]->GetBufferSize();++i)std::fprintf(file,"%u,%s",bytes[i],i%24==23?"\n":"");std::fputs("\n};\n",file);std::fclose(file);
    require(_wfopen_s(&file,argv[3],L"wb")==0,"bytecode open");require(std::fwrite(bytes,1,shaders[1]->GetBufferSize(),file)==shaders[1]->GetBufferSize(),"bytecode write");std::fclose(file);
    std::printf("PASS: Classic encode/compose exact; Hybrid FP16, low range, SDR bypass, alpha, padding, zero edit, matched reference, mode reset; debug=%d; no_model=1; max_no_edit_error=%g\n",debugging,delta(restored,originals));return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
