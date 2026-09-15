// Real copy/fence/readback on WARP, not an NR runtime or gameplay test.
#define NOMINMAX
#define wmain pixel_baseline_main
#include "test-nr-output-pixels.cpp"
#undef wmain
#include <dlssnr/LayerCapture.h>
#include <fstream>

int wmain(int argc,wchar_t** argv)try {
    require(argc==2,"output root required");const std::filesystem::path out=argv[1];
    ComPtr<ID3D12Debug> debug;check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> warp;check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    ComPtr<ID3D12Device> dev;check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
    ComPtr<ID3D12CommandQueue> q;D3D12_COMMAND_QUEUE_DESC qd{};check(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator> alloc;check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> cmd;check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&cmd)));
    auto image=create(dev.Get(),texture(640,384,true),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    const auto desc=image->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    dev->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
    std::array<ComPtr<ID3D12Resource>,4> uploads;
    for(unsigned s=0;s<4;++s){uploads[s]=buffer(dev.Get(),bytes,false);void* mem=nullptr;D3D12_RANGE no{};
        check(uploads[s]->Map(0,&no,&mem));
        for(unsigned y=0;y<384;++y)for(unsigned x=0;x<640;++x){const Pixel p{float(s),float(x),float(y),1};
            std::memcpy(static_cast<char*>(mem)+size_t(y)*fp.Footprint.RowPitch+x*sizeof(Pixel),p.data(),sizeof(p));}
        uploads[s]->Unmap(0,nullptr);
    }
    const auto upload=[&](unsigned s){auto src=location(uploads[s].Get());src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        auto dst=location(image.Get());cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);};
    const auto submit=[&](capture::LayerCapture& c,bool blocked){
        check(cmd->Close());ID3D12CommandList* lists[]{cmd.Get()};
        ComPtr<ID3D12Fence> gate,done;check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
        if(blocked)check(q->Wait(gate.Get(),1));
        q->ExecuteCommandLists(1,lists);c.submitted(q.Get(),1,lists);check(q->Signal(done.Get(),1));
        if(blocked){require(!c.completed(),"submitted but GPU blocked is not completion");require(!c.releaseCompleted(),"pending allocation must not release");
            require(c.write(out).state==capture::WriteResult::State::Idle,"pending cannot write");check(gate->Signal(1));}
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"event");check(done->SetEventOnCompletion(1,event));
        require(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"completed in time");CloseHandle(event);
        check(alloc->Reset());check(cmd->Reset(alloc.Get(),nullptr));
    };
    capture::LayerCapture c;require(c.prepare(dev.Get(),{image.Get(),image.Get(),image.Get(),image.Get()},8),"prepare");
    require(c.bytes()<=128ull*1024*1024,"allocation bounded");
    for(unsigned f=0;f<8;++f){capture::LayerCapture::Metadata m{};m.frame=f;m.attempt=f+1;m.source=77;m.shared=true;
        require(c.begin(dev.Get(),cmd.Get(),m),"begin");
        for(unsigned s=0;s<4;++s){upload(s);barrier(cmd.Get(),image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            c.copy(cmd.Get(),s,image.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // Overwrite the same texture at each stage: proves preservation before reuse and restoration.
            barrier(cmd.Get(),image.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);}
        c.end();submit(c,f==7);
    }
    require(c.completed(),"eight complete frames");auto written=c.write(out);require(written.state==capture::WriteResult::State::Success,"write");
    const unsigned xs[]{256,96,416,96,416},ys[]{128,32,32,224,224};
    for(unsigned f=0;f<8;++f)for(unsigned s=0;s<4;++s)for(unsigned r=0;r<5;++r){
        char name[64];std::snprintf(name,sizeof(name),"f%02u_s%u_r%u.raw",f,s,r);
        std::ifstream file(std::filesystem::path(written.directory)/name,std::ios::binary);require(bool(file),"raw exists");
        for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){Pixel pixel;file.read(reinterpret_cast<char*>(pixel.data()),sizeof(pixel));
            require(bool(file)&&pixel==Pixel{float(s),float(xs[r]+x),float(ys[r]+y),1},"same-frame region/stage pixels exact");}
        require(file.peek()==EOF,"tight raw length");
    }
    puts("PASS eight frames, five regions, four overwritten stages, exact pixels and blocked GPU gate");
    // Partial frame is retained as incomplete; no automatic retry and no fake full chain.
    require(c.prepare(dev.Get(),{image.Get(),image.Get(),image.Get(),image.Get()},2),"partial prepare");
    capture::LayerCapture::Metadata m{};m.attempt=1;m.source=9;require(c.begin(dev.Get(),cmd.Get(),m),"partial begin");
    upload(0);c.copy(cmd.Get(),0,image.Get(),D3D12_RESOURCE_STATE_COPY_DEST);c.end();
    submit(c,false);written=c.write(out);require(written.state==capture::WriteResult::State::Success && std::string(written.reason)=="missing_stage","partial evidence retained");
    require(c.prepare(dev.Get(),{image.Get(),image.Get(),image.Get(),image.Get()},2),"source change prepare");
    require(c.begin(dev.Get(),cmd.Get(),m),"source frame");for(unsigned s=0;s<4;++s)c.copy(cmd.Get(),s,image.Get(),D3D12_RESOURCE_STATE_COPY_DEST);c.end();submit(c,false);
    m.attempt=2;m.source=10;require(!c.begin(dev.Get(),cmd.Get(),m),"source change refused");
    written=c.write(out);require(written.state==capture::WriteResult::State::Success && std::string(written.reason)=="source_mode_or_recording_gap","source change incomplete");
    require(!c.prepare(dev.Get(),{image.Get(),image.Get(),image.Get(),image.Get()},8),"session limit");
    capture::LayerCapture failure;require(failure.prepare(dev.Get(),{image.Get(),image.Get(),image.Get(),image.Get()},1),"write failure prepare");
    require(failure.begin(dev.Get(),cmd.Get(),m),"write failure begin");for(unsigned s=0;s<4;++s)failure.copy(cmd.Get(),s,image.Get(),D3D12_RESOURCE_STATE_COPY_DEST);failure.end();submit(failure,false);
    capture::FileIo io;io.open=[](const wchar_t*,const wchar_t*)->std::FILE*{return nullptr;};
    auto failed=failure.write(out/"failure",io);require(failed.state==capture::WriteResult::State::Failed&&!failure.pending(),"write failure releases only completed readback");
    require(!std::filesystem::exists(std::filesystem::path(failed.directory)/"manifest.json"),"no false completion marker");
    capture::LayerCapture contract;auto tinyTexture=create(dev.Get(),texture(64,64,true),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    require(!contract.prepare(dev.Get(),{tinyTexture.Get(),tinyTexture.Get(),tinyTexture.Get(),tinyTexture.Get()},8),"small extent rejected");
    // The game model uses RGBA16F: exercise that copy footprint and verify raw half bits too.
    auto halfDesc=texture(128,128,true);halfDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto halfImage=create(dev.Get(),halfDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto halfUpload=buffer(dev.Get(),128*128*8,false);void* halfMemory=nullptr;D3D12_RANGE empty{};
    check(halfUpload->Map(0,&empty,&halfMemory));
    const std::array<uint16_t,4> expectedHalf{0x3800,0x3c00,0x4000,0x3c00};
    for(unsigned i=0;i<128*128;++i)std::memcpy(static_cast<char*>(halfMemory)+i*8,expectedHalf.data(),8);
    halfUpload->Unmap(0,nullptr);
    auto halfFrom=location(halfUpload.Get());halfFrom.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    halfFrom.PlacedFootprint={0,{DXGI_FORMAT_R16G16B16A16_FLOAT,128,128,1,1024}};
    auto halfTo=location(halfImage.Get());cmd->CopyTextureRegion(&halfTo,0,0,0,&halfFrom,nullptr);
    capture::LayerCapture halfCapture;require(halfCapture.prepare(dev.Get(),{halfImage.Get(),halfImage.Get(),halfImage.Get(),halfImage.Get()},1),"half prepare");
    require(halfCapture.bytes()==5*4*128*128*8,"half memory footprint");
    require(halfCapture.begin(dev.Get(),cmd.Get(),m),"half begin");
    for(unsigned s=0;s<4;++s)halfCapture.copy(cmd.Get(),s,halfImage.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
    halfCapture.end();submit(halfCapture,false);written=halfCapture.write(out/"half");require(written.state==capture::WriteResult::State::Success,"half write");
    std::ifstream halfFile(std::filesystem::path(written.directory)/"f00_s2_r4.raw",std::ios::binary);
    for(unsigned i=0;i<128*128;++i){std::array<uint16_t,4> v{};halfFile.read(reinterpret_cast<char*>(v.data()),8);require(bool(halfFile)&&v==expectedHalf,"half raw bits exact");}
    require(halfFile.peek()==EOF,"half length");puts("PASS RGBA16F copy/readback exact half bits");
    check(cmd->Close());
    ComPtr<ID3D12InfoQueue> info;check(dev.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n=0;info->GetMessage(i,nullptr,&n);std::vector<char> data(n);
        auto* msg=reinterpret_cast<D3D12_MESSAGE*>(data.data());check(info->GetMessage(i,msg,&n));if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(msg->pDescription);throw std::runtime_error("debug error");}}
    puts("PASS partial/source-change/session-limit/write-failure/unsupported-contract; D3D12 debug clean");return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
