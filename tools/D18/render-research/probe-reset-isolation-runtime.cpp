#define wmain baseline_test_main
#include "test-nr-output-pixels.cpp"
#undef wmain
#include "../D24/dx11_probe_parameters.h"
#include <DirectXPackedVector.h>
int wmain(int argc,wchar_t** argv)try{
    require(argc==10,"forwarder runtime data-path driver-core mode output-directory style frozen-input reset-policy");
    const std::wstring resetPolicy=argv[9];require(resetPolicy==L"initial"||resetPolicy==L"every_call","reset policy");
    const bool resetEveryCall=resetPolicy==L"every_call";
    const int style=_wtoi(argv[7]);require(style==0||style==2,"style 0 or 2");
    const std::wstring mode=argv[5];
    require(mode==L"feedback_live_shared"||mode==L"feedback_live_independent"||mode==L"feedback_frozen_shared"||mode==L"feedback_frozen_independent"||mode==L"feedback_live_split_params","unsupported probe mode");
    const bool shared=mode!=L"feedback_live_independent"&&mode!=L"feedback_frozen_independent";
    const bool frozen=mode==L"feedback_frozen_shared"||mode==L"feedback_frozen_independent",splitParams=mode==L"feedback_live_split_params";
    const std::filesystem::path outputDirectory=argv[6];
    std::filesystem::create_directories(outputDirectory);
    ComPtr<ID3D12Debug> debug;const bool dbg=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));if(dbg)debug->EnableDebugLayer();printf("debug_layer=%u style=%d warmup=48 measured=16\n",unsigned(dbg),style);
    ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(d.VendorId==0x10de){std::wprintf(L"GPU %s\n",d.Description);break;}adapter.Reset();}
    require(adapter!=nullptr,"NVIDIA adapter");ComPtr<ID3D12Device> dev;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> cmd;check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&cmd)));
    ComPtr<ID3D12Fence> fence;check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));UINT64 serial=0;
    auto submit=[&]{check(cmd->Close());ID3D12CommandList* list[]={cmd.Get()};queue->ExecuteCommandLists(1,list);check(queue->Signal(fence.Get(),++serial));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"event");check(fence->SetEventOnCompletion(serial,event));
        const auto wait=WaitForSingleObject(event,10000);CloseHandle(event);require(wait==WAIT_OBJECT_0 && fence->GetCompletedValue()!=UINT64_MAX,"runtime GPU completion");
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
    DXGI_QUERY_VIDEO_MEMORY_INFO admission{};check(memoryAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&admission));
    require(admission.Budget>admission.CurrentUsage && admission.Budget-admission.CurrentUsage>2ull*1024*1024*1024,"probe needs 2 GiB headroom");
    NVSDK_NGX_Parameter* params[2]{};void* features[2]{};
    using Destroy=int(*)(NVSDK_NGX_Parameter*);Destroy destroy=nullptr;
    {HMODULE core=LoadLibraryExW(argv[4],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);require(core!=nullptr,"driver core load");
        using Init=int(*)(unsigned long long,const wchar_t*,ID3D12Device*,int,void*);
        auto init=reinterpret_cast<Init>(GetProcAddress(core,"NVSDK_NGX_D3D12_Init_Ext"));require(init!=nullptr,"driver init export");
        const int result=init(0,argv[3],dev.Get(),0x15,nullptr);printf("driver init=%08X\n",unsigned(result));require(result==1,"driver init");
        auto get=reinterpret_cast<int(*)(NVSDK_NGX_Parameter**)>(GetProcAddress(core,"NVSDK_NGX_D3D12_GetCapabilityParameters"));
        destroy=reinterpret_cast<Destroy>(GetProcAddress(core,"NVSDK_NGX_D3D12_DestroyParameters"));require(get&&destroy,"capability exports");
        for(unsigned i=0;i<2;++i){require(get(&params[i])==1&&params[i],"driver capability block");for(unsigned j=0;j<i;++j)require(params[i]!=params[j],"distinct capability blocks");}
        bool found=false;auto probe=reinterpret_cast<void(*)(void*,const char*,float,int)>(symbol("dlssnr_call_probe_float"));
        for(int slot:{1,2,5,6,7,4,3,0}){float value=0;probe(params[0],"D18.MultipassProbe",.375f,slot);if(params[0]->Get("D18.MultipassProbe",&value)==NVSDK_NGX_Result_Success&&value==.375f){reinterpret_cast<void(*)(int)>(symbol("dlssnr_call_set_float_slot"))(slot);found=true;printf("driver float slot=%d\n",slot);break;}}
        require(found,"driver parameter float slot");
    }
    const UINT w=640,h=384,gw=320,gh=192;
    const float ratios[2]={1.f,1.f};
    for(unsigned i=0;i<2;++i){features[i]=createFeature(argv[2],argv[3],dev.Get(),cmd.Get(),params[i],w,h,0,1.f,style,1,1,-1,1,1,ratios[i]);
        submit();require(features[i]!=nullptr,"model creation rejected");
        for(unsigned j=0;j<i;++j)require(features[i]!=features[j],"distinct handles");memory("after_create");}
    std::vector<ComPtr<ID3D12Resource>> uploads;
    auto input=[&](UINT iw,UINT ih,DXGI_FORMAT format,const float values[4]){
        auto d=texture(iw,ih,false);d.Format=format;auto dst=create(dev.Get(),d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto up=buffer(dev.Get(),bytes,false);
        unsigned char* mem=nullptr;D3D12_RANGE empty{};check(up->Map(0,&empty,reinterpret_cast<void**>(&mem)));
        UINT pixelBytes=format==DXGI_FORMAT_R32_FLOAT?4:format==DXGI_FORMAT_R32G32_FLOAT?8:8;
        unsigned short half[4];for(int c=0;c<4;++c)half[c]=DirectX::PackedVector::XMConvertFloatToHalf(values[c]);
        for(UINT y=0;y<ih;++y)for(UINT x=0;x<iw;++x){
            if(format==DXGI_FORMAT_R16G16B16A16_FLOAT){
                // Static dark bands, bright edges and deterministic fine texture.
                // It is a synthetic already encoded image, not a captured game input.
                float v=.025f+.015f*float((x/64+y/48)%4)+float((x*13+y*7)%17)*.0005f;
                if(x%160<12)v=.65f; if(y<24)v=.25f;
                for(unsigned c=0;c<3;++c)half[c]=DirectX::PackedVector::XMConvertFloatToHalf(v*(1.f-.04f*float(c)));
                half[3]=DirectX::PackedVector::XMConvertFloatToHalf(1.f);
            }
            memcpy(mem+y*fp.Footprint.RowPitch+x*pixelBytes,
              format==DXGI_FORMAT_R16G16B16A16_FLOAT?static_cast<const void*>(half):values,pixelBytes);
        }
        up->Unmap(0,nullptr);auto from=location(up.Get()),to=location(dst.Get());from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
        cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);barrier(cmd.Get(),dst.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);uploads.push_back(up);return dst;
    };
    float rgb[4]={.4f,.5f,.6f,1},z[4]={.5f,0,0,0},mv[4]={0,0,0,0};
    auto color=input(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,rgb),depth=input(gw,gh,DXGI_FORMAT_R32_FLOAT,z),motion=input(gw,gh,DXGI_FORMAT_R32G32_FLOAT,mv);
    auto outputDesc=texture(w,h,true);outputDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    float zero[4]={0,0,0,0};auto zeroMotion=input(gw,gh,DXGI_FORMAT_R32G32_FLOAT,zero);
    auto output=create(dev.Get(),outputDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto second=create(dev.Get(),outputDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);submit();
    auto scratchDesc=texture(w,h,false);scratchDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto fixedInput=create(dev.Get(),scratchDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto frozenInput=create(dev.Get(),scratchDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    {
        FILE* f=nullptr;require(_wfopen_s(&f,argv[8],L"rb")==0&&f,"open frozen input");
        std::vector<unsigned char> raw(size_t(w)*h*8);const auto count=fread(raw.data(),1,raw.size(),f);
        const int extra=fgetc(f);const bool readError=ferror(f)!=0;const int close=fclose(f);
        require(count==raw.size()&&extra==EOF&&!readError&&close==0,"frozen input size/read");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT frozenFp{};UINT64 frozenBytes=0;
        dev->GetCopyableFootprints(&scratchDesc,0,1,0,&frozenFp,nullptr,nullptr,&frozenBytes);
        auto up=buffer(dev.Get(),frozenBytes,false);unsigned char* mapped=nullptr;D3D12_RANGE empty{};
        check(up->Map(0,&empty,reinterpret_cast<void**>(&mapped)));
        for(UINT y=0;y<h;++y)memcpy(mapped+y*frozenFp.Footprint.RowPitch,raw.data()+size_t(y)*w*8,w*8);
        up->Unmap(0,nullptr);auto from=location(up.Get()),to=location(frozenInput.Get());
        from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=frozenFp;
        cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        barrier(cmd.Get(),frozenInput.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        uploads.push_back(up);submit();
    }
    // The model sees one input resource in every pass/frame in all three modes.
    // All modes issue exactly one full copy per model call; only copied pixels differ.
    auto setFixedInput=[&](ID3D12Resource* source){
        barrier(cmd.Get(),source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(cmd.Get(),fixedInput.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(fixedInput.Get(),source);
        barrier(cmd.Get(),fixedInput.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(cmd.Get(),source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    DlssNrAbi::Frame rect;rect.color=rect.output={0,0,w,h};rect.depth=rect.motion={0,0,gw,gh};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;dev->GetCopyableFootprints(&outputDesc,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto firstReadback=buffer(dev.Get(),bytes,true),secondReadback=buffer(dev.Get(),bytes,true);
    auto copyReadback=[&](ID3D12Resource* src,ID3D12Resource* rb,D3D12_RESOURCE_STATES state){
        barrier(cmd.Get(),src,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        auto from=location(src),to=location(rb);to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
        cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);barrier(cmd.Get(),src,D3D12_RESOURCE_STATE_COPY_SOURCE,state);
    };
    auto dump=[&](ID3D12Resource* rb,const wchar_t* stage,unsigned frame){
        unsigned char* mem=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};check(rb->Map(0,&range,reinterpret_cast<void**>(&mem)));
        double sum=0;float lo=1e30f,hi=-1e30f;
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto* pixel=reinterpret_cast<unsigned short*>(mem+y*fp.Footprint.RowPitch+x*8);
            for(unsigned c=0;c<3;++c){float v=DirectX::PackedVector::XMConvertHalfToFloat(pixel[c]);require(std::isfinite(v),"nonfinite model output");sum+=v;lo=(std::min)(lo,v);hi=(std::max)(hi,v);}}
        const auto name=outputDirectory/(std::wstring(stage)+L"_"+std::to_wstring(frame)+L".rgba16f");
        FILE* f=nullptr;require(_wfopen_s(&f,name.c_str(),L"wb")==0&&f,"open pixel dump");
        bool ok=true;for(UINT y=0;y<h;++y)if(fwrite(mem+y*fp.Footprint.RowPitch,1,w*8,f)!=w*8)ok=false;
        const int close=fclose(f);D3D12_RANGE empty{};rb->Unmap(0,&empty);require(ok&&close==0,"pixel dump write");
        printf("PIXELS frame=%u stage=%ls mean=%.9f min=%.9f max=%.9f\n",frame,stage,sum/(double(w)*h*3),lo,hi);
    };
    copyReadback(color.Get(),firstReadback.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);submit();dump(firstReadback.Get(),L"input",0);
    copyReadback(frozenInput.Get(),firstReadback.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);submit();dump(firstReadback.Get(),L"frozen",0);
    for(unsigned frame=0;frame<64;++frame){
        ID3D12Resource* previous=color.Get();
        for(unsigned pass=0;pass<2;++pass){
            const unsigned instance=shared?0:pass;auto* answer=pass==0?output.Get():second.Get();
            const bool reset=resetEveryCall||(frame==0&&(!shared||pass==0));
            setFixedInput(frozen&&pass==1?frozenInput.Get():previous);
            const unsigned paramIndex=splitParams?pass:instance;
            if(frame==0)printf("CALL pass=%u feature=%u params=%u reset=%u frozen=%u\n",pass,instance,paramIndex,unsigned(reset),unsigned(frozen&&pass==1));
            const int result=eval(cmd.Get(),features[instance],params[paramIndex],fixedInput.Get(),depth.Get(),shared&&pass>0?zeroMotion.Get():motion.Get(),answer,
                w,h,gw,gh,0,reset,1,style,1,1,-1,1,float(gw),float(gh),ratios[instance],&rect);
            require(result==1,"model evaluate rejected");
            barrier(cmd.Get(),answer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            copyReadback(answer,pass==0?firstReadback.Get():secondReadback.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            previous=answer;

        }
        barrier(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(cmd.Get(),second.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        submit();if(frame>=48){dump(firstReadback.Get(),L"pass1",frame);dump(secondReadback.Get(),L"pass2",frame);}fflush(stdout);
    }
    memory("after_probe");
    for(auto* f:features)reinterpret_cast<void(*)(void*)>(symbol("dlssnr_call_release"))(f);
    if(destroy)for(auto* param:params)require(destroy(param)==1,"destroy capability block");
    if(dbg){ComPtr<ID3D12InfoQueue> info;check(dev.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T messageBytes=0;info->GetMessage(i,nullptr,&messageBytes);std::vector<char> data(messageBytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());check(info->GetMessage(i,message,&messageBytes));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(message->pDescription);throw std::runtime_error("D3D12 validation error");}}}
    printf("PASS real NR finite stage readbacks mode=%ls 640x384 64 frames (last 16 saved); synthetic input, no game acceptance\n",mode.c_str());return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
