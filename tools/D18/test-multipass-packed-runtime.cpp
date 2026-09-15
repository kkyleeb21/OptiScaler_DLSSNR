#define wmain baseline_test_main
#include "test-nr-output-pixels.cpp"
#undef wmain
#include "../D24/dx11_probe_parameters.h"
#include <DirectXPackedVector.h>
int wmain(int argc,wchar_t** argv)try{
    require(argc==4 || argc==5,"forwarder runtime data-path [driver-core]");
    ComPtr<ID3D12Debug> debug;const bool dbg=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));if(dbg)debug->EnableDebugLayer();
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
    ComPtr<IDXGIAdapter3> memoryAdapter;check(adapter.As(&memoryAdapter));
    auto memory=[&](const char* stage){DXGI_QUERY_VIDEO_MEMORY_INFO info{};check(memoryAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info));
        std::printf("memory stage=%s local_usage=%llu budget=%llu\n",stage,info.CurrentUsage,info.Budget);};
    ComPtr<ID3D12QueryHeap> timestamps;D3D12_QUERY_HEAP_DESC qd{};qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qd.Count=2;check(dev->CreateQueryHeap(&qd,IID_PPV_ARGS(&timestamps)));
    auto timingReadback=buffer(dev.Get(),2*sizeof(UINT64),true);UINT64 frequency=0;check(queue->GetTimestampFrequency(&frequency));
    memory("before_features");
    ProbeParameters probeParams[4];NVSDK_NGX_Parameter* params[4]{};void* features[4]{};
    using Destroy=int(*)(NVSDK_NGX_Parameter*);Destroy destroy=nullptr;
    if(argc==5){HMODULE core=LoadLibraryExW(argv[4],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);require(core!=nullptr,"driver core load");
        using Init=int(*)(unsigned long long,const wchar_t*,ID3D12Device*,int,void*);
        auto init=reinterpret_cast<Init>(GetProcAddress(core,"NVSDK_NGX_D3D12_Init_Ext"));require(init!=nullptr,"driver init export");
        const int result=init(0,argv[3],dev.Get(),0x15,nullptr);printf("driver init=%08X\n",unsigned(result));require(result==1,"driver init");
        auto get=reinterpret_cast<int(*)(NVSDK_NGX_Parameter**)>(GetProcAddress(core,"NVSDK_NGX_D3D12_GetCapabilityParameters"));
        destroy=reinterpret_cast<Destroy>(GetProcAddress(core,"NVSDK_NGX_D3D12_DestroyParameters"));require(get&&destroy,"capability exports");
        for(unsigned i=0;i<4;++i){require(get(&params[i])==1&&params[i],"driver capability block");for(unsigned j=0;j<i;++j)require(params[i]!=params[j],"distinct capability blocks");}
        bool found=false;auto probe=reinterpret_cast<void(*)(void*,const char*,float,int)>(symbol("dlssnr_call_probe_float"));
        for(int slot:{1,2,5,6,7,4,3,0}){float value=0;probe(params[0],"D18.MultipassProbe",.375f,slot);if(params[0]->Get("D18.MultipassProbe",&value)==NVSDK_NGX_Result_Success&&value==.375f){reinterpret_cast<void(*)(int)>(symbol("dlssnr_call_set_float_slot"))(slot);found=true;printf("driver float slot=%d\n",slot);break;}}
        require(found,"driver parameter float slot");
    }else for(unsigned i=0;i<4;++i)params[i]=&probeParams[i];
    const UINT w=1920,h=1080,gw=960,gh=540;
    const float ratios[4]={.5f,2.f/3,.75f,1.f};
    for(unsigned i=0;i<4;++i){features[i]=createFeature(argv[2],argv[3],dev.Get(),cmd.Get(),params[i],w,h,0,1.f-float(i)*.1f,0,1,1,-1,1,1,ratios[i]);
        submit();require(features[i]!=nullptr,"model creation rejected");
        for(unsigned j=0;j<i;++j)require(features[i]!=features[j],"distinct handles");memory("after_create");}
    std::vector<ComPtr<ID3D12Resource>> uploads;
    auto input=[&](UINT iw,UINT ih,DXGI_FORMAT format,const float values[4]){
        auto d=texture(iw,ih,false);d.Format=format;auto dst=create(dev.Get(),d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto up=buffer(dev.Get(),bytes,false);
        unsigned char* mem=nullptr;D3D12_RANGE empty{};check(up->Map(0,&empty,reinterpret_cast<void**>(&mem)));
        UINT pixelBytes=(format==DXGI_FORMAT_R32_FLOAT || format==DXGI_FORMAT_R11G11B10_FLOAT)?4:8;
        DirectX::PackedVector::XMFLOAT3PK packed(values[0],values[1],values[2]);
        unsigned short half[4];for(int c=0;c<4;++c)half[c]=DirectX::PackedVector::XMConvertFloatToHalf(values[c]);
        for(UINT y=0;y<ih;++y)for(UINT x=0;x<iw;++x)memcpy(mem+y*fp.Footprint.RowPitch+x*pixelBytes,
            format==DXGI_FORMAT_R16G16B16A16_FLOAT?static_cast<const void*>(half):format==DXGI_FORMAT_R11G11B10_FLOAT?static_cast<const void*>(&packed):values,pixelBytes);
        up->Unmap(0,nullptr);auto from=location(up.Get()),to=location(dst.Get());from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
        cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);barrier(cmd.Get(),dst.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);uploads.push_back(up);return dst;
    };
    float rgb[4]={.4f,.5f,.6f,1},z[4]={.5f,0,0,0},mv[4]={1.f/gw,-2.f/gh,0,0};
    auto color=input(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,rgb),depth=input(gw,gh,DXGI_FORMAT_R32_FLOAT,z),motion=input(gw,gh,DXGI_FORMAT_R32G32_FLOAT,mv);
    auto outputDesc=texture(w,h,true);outputDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    float zero[4]={0,0,0,0};auto zeroMotion=input(gw,gh,DXGI_FORMAT_R32G32_FLOAT,zero);
    outputDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto output=create(dev.Get(),outputDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    outputDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto second=create(dev.Get(),outputDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);submit();
    DlssNrAbi::Frame rect;rect.color=rect.output={0,0,w,h};rect.depth=rect.motion={0,0,gw,gh};
    const unsigned counts[]={1,2,3,4,4,2,1,4,4,4,4,4,4,4};
    for(unsigned frame=0;frame<14;++frame){
        const bool shared=frame>=8&&frame<12;const unsigned count=counts[frame];
        ID3D12Resource* previous=color.Get();
        cmd->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        for(unsigned pass=0;pass<count;++pass){auto* answer=pass%2==0?output.Get():second.Get();
            if(pass>=2)barrier(cmd.Get(),answer,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const unsigned instance=shared?0:pass;
            const bool reset=shared?(pass==0&&frame==8):(frame<4||frame==7||frame==12);
            const int result=eval(cmd.Get(),features[instance],params[instance],previous,depth.Get(),shared&&pass>0?zeroMotion.Get():motion.Get(),answer,
                w,h,gw,gh,0,reset,1.f-float(instance)*.1f,0,1,1,-1,1,float(gw),float(gh),ratios[instance],&rect);
            require(result==1,"pass evaluate rejected");
            barrier(cmd.Get(),answer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);previous=answer;
            printf("frame=%u pass=%u shared=%u reset=%u ratio=%.3f result=%08X\n",frame,pass+1,shared,reset,ratios[instance],unsigned(result));
        }
        barrier(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(count>1)barrier(cmd.Get(),second.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        cmd->ResolveQueryData(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,timingReadback.Get(),0);
        submit();UINT64* ticks=nullptr;D3D12_RANGE range{0,2*sizeof(UINT64)};
        check(timingReadback->Map(0,&range,reinterpret_cast<void**>(&ticks)));
        printf("GPU_completed frame=%u count=%u shared=%u model_interval_ms=%.4f\n",frame,count,shared,double(ticks[1]-ticks[0])*1000/frequency);
        D3D12_RANGE empty{};timingReadback->Unmap(0,&empty);memory("after_evaluate");fflush(stdout);
    }
    output.Swap(second); // Read the final second-pass answer.
    auto d=output->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto rb=buffer(dev.Get(),bytes,true);
    barrier(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);auto from=location(output.Get()),to=location(rb.Get());to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);submit();
    unsigned char* mem=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};check(rb->Map(0,&range,reinterpret_cast<void**>(&mem)));double mean=0;
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto* p=reinterpret_cast<unsigned short*>(mem+y*fp.Footprint.RowPitch+x*8);for(int c=0;c<3;++c){float v=DirectX::PackedVector::XMConvertHalfToFloat(p[c]);require(std::isfinite(v),"nonfinite model output");mean+=v;}}
    mean/=double(w)*h*3;std::printf("mean=%f model pixels completed\n",mean);require(mean>.01 && mean<2,"implausible model output");
    D3D12_RANGE empty{};rb->Unmap(0,&empty);for(auto* f:features)reinterpret_cast<void(*)(void*)>(symbol("dlssnr_call_release"))(f);
    if(destroy)for(auto* param:params)require(destroy(param)==1,"destroy capability block");
    if(dbg){ComPtr<ID3D12InfoQueue> info;check(dev.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T messageBytes=0;info->GetMessage(i,nullptr,&messageBytes);std::vector<char> data(messageBytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());check(info->GetMessage(i,message,&messageBytes));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(message->pDescription);throw std::runtime_error("D3D12 validation error");}}}
    puts("PASS private FP16 model surfaces for format-independent multipass, four distinct instances, mixed ratios, 1-4 counts, shared zero-motion repeats, history mode changes, GPU completion and finite final pixels; game quality not proven");return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
