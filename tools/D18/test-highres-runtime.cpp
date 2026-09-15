#define wmain baseline_test_main
#include "test-nr-output-pixels.cpp"
#undef wmain
#include "../D24/dx11_probe_parameters.h"
#include <DirectXPackedVector.h>
int wmain(int argc,wchar_t** argv)try{
    require(argc==4 || argc==6,"forwarder runtime data-path [width height]");
    ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(d.VendorId==0x10de){std::wprintf(L"GPU %s\n",d.Description);break;}adapter.Reset();}
    require(adapter!=nullptr,"NVIDIA adapter");ComPtr<ID3D12Device> dev;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> cmd;check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&cmd)));
    ComPtr<ID3D12Fence> fence;check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));UINT64 serial=0;
    auto submit=[&]{check(cmd->Close());ID3D12CommandList* list[]={cmd.Get()};queue->ExecuteCommandLists(1,list);check(queue->Signal(fence.Get(),++serial));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"event");check(fence->SetEventOnCompletion(serial,event));
        const auto wait=WaitForSingleObject(event,30000);CloseHandle(event);require(wait==WAIT_OBJECT_0 && fence->GetCompletedValue()!=UINT64_MAX,"runtime GPU completion");
        check(allocator->Reset());check(cmd->Reset(allocator.Get(),nullptr));};
    HMODULE fw=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);require(fw!=nullptr,"forwarder load");
    auto symbol=[&](const char* name){auto p=GetProcAddress(fw,name);require(p!=nullptr,name);return p;};
    using Create=void*(*)(const wchar_t*,const wchar_t*,ID3D12Device*,ID3D12GraphicsCommandList*,void*,unsigned,unsigned,int,float,int,float,float,float,int,int,float);
    using Eval=int(*)(ID3D12GraphicsCommandList*,void*,void*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,unsigned,unsigned,unsigned,unsigned,int,int,float,int,float,float,float,int,float,float,float,const DlssNrAbi::Frame*);
    auto createFeature=reinterpret_cast<Create>(symbol("dlssnr_call_create"));auto eval=reinterpret_cast<Eval>(symbol("dlssnr_call_evaluate_v2"));
    reinterpret_cast<void(*)(int)>(symbol("dlssnr_call_set_float_slot"))(6);
    reinterpret_cast<int(*)(int,int)>(symbol("dlssnr_call_set_sampler_modes"))(1,1);
    ProbeParameters params;
    const UINT w=argc==6?UINT(_wtoi(argv[4])):4800,h=argc==6?UINT(_wtoi(argv[5])):2704,gw=1920,gh=1080;
    require(w>3840 && h>2160 && w<=5760 && h<=3240 && w%16==0 && h%8==0,"bounded high-resolution geometry");
    printf("base=3840x2160 work=%ux%u guides=%ux%u ratio=1 format=RGBA16F\n",w,h,gw,gh);
    auto* feature=createFeature(argv[2],argv[3],dev.Get(),cmd.Get(),&params,w,h,0,.5f,0,.5f,.5f,.5f,1,1,1.f);
    std::printf("create init=%08X create=%08X handle=%p\n",*reinterpret_cast<unsigned*>(symbol("dlssnr_call_last_init")),*reinterpret_cast<unsigned*>(symbol("dlssnr_call_last_create")),feature);fflush(stdout);
    submit();require(feature!=nullptr,"model creation rejected");
    std::vector<ComPtr<ID3D12Resource>> uploads;
    auto input=[&](UINT iw,UINT ih,DXGI_FORMAT format,const float values[4]){
        auto d=texture(iw,ih,false);d.Format=format;auto dst=create(dev.Get(),d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto up=buffer(dev.Get(),bytes,false);
        unsigned char* mem=nullptr;D3D12_RANGE empty{};check(up->Map(0,&empty,reinterpret_cast<void**>(&mem)));
        UINT pixelBytes=format==DXGI_FORMAT_R32_FLOAT?4:format==DXGI_FORMAT_R32G32_FLOAT?8:8;
        unsigned short half[4];for(int c=0;c<4;++c)half[c]=DirectX::PackedVector::XMConvertFloatToHalf(values[c]);
        for(UINT y=0;y<ih;++y)for(UINT x=0;x<iw;++x)memcpy(mem+y*fp.Footprint.RowPitch+x*pixelBytes,
            format==DXGI_FORMAT_R16G16B16A16_FLOAT?static_cast<const void*>(half):values,pixelBytes);
        up->Unmap(0,nullptr);auto from=location(up.Get()),to=location(dst.Get());from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
        cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);barrier(cmd.Get(),dst.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);uploads.push_back(up);return dst;
    };
    float rgb[4]={.4f,.5f,.6f,1},z[4]={.5f,0,0,0},mv[4]={1.f/gw,-2.f/gh,0,0};
    auto color=input(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,rgb),depth=input(gw,gh,DXGI_FORMAT_R32_FLOAT,z),motion=input(gw,gh,DXGI_FORMAT_R32G32_FLOAT,mv);
    auto outputDesc=texture(w,h,true);outputDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto output=create(dev.Get(),outputDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);submit();
    DlssNrAbi::Frame rect;rect.color=rect.output={0,0,w,h};rect.depth=rect.motion={0,0,gw,gh};
    for(unsigned i=0;i<4;++i){int result=eval(cmd.Get(),feature,&params,color.Get(),depth.Get(),motion.Get(),output.Get(),w,h,gw,gh,0,i==0,.5f,0,.5f,.5f,.5f,1,float(gw),float(gh),1,&rect);
        std::printf("evaluate %u result=%08X\n",i,unsigned(result));fflush(stdout);submit();require(result==1,"evaluate rejected");}
    auto d=output->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto rb=buffer(dev.Get(),bytes,true);
    barrier(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);auto from=location(output.Get()),to=location(rb.Get());to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);submit();
    unsigned char* mem=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};check(rb->Map(0,&range,reinterpret_cast<void**>(&mem)));double mean=0;
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto* p=reinterpret_cast<unsigned short*>(mem+y*fp.Footprint.RowPitch+x*8);for(int c=0;c<3;++c){float v=DirectX::PackedVector::XMConvertHalfToFloat(p[c]);require(std::isfinite(v),"nonfinite model output");mean+=v;}}
    mean/=double(w)*h*3;std::printf("mean=%f model pixels completed\n",mean);require(mean>.01 && mean<2,"implausible model output");
    D3D12_RANGE empty{};rb->Unmap(0,&empty);reinterpret_cast<void(*)(void*)>(symbol("dlssnr_call_release"))(feature);
    puts("PASS real DX12 runtime creation, four evaluations, GPU fence and pixel readback; gameplay and moving MV not tested");return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
