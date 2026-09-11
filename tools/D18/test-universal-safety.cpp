#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <dlssnr/SubmissionEvidence.h>
#include <dlssnr/RetirementPolicy.h>
#include <dlssnr/GuideCopyContract.h>
#include <dlssnr/PublishedSnapshot.h>
#include <dlssnr/DlssNr_Capture.h>
#include <atomic>
#include <thread>
#include <vector>
#include <array>
#include <chrono>
#include <fstream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void require(bool v,const char* what){if(!v)throw std::runtime_error(what);}
void check(HRESULT r){require(SUCCEEDED(r),"D3D12 operation failed");}
void policyTests(){
    using namespace DlssNr::Retirement;
    require(!ObservedLastUse(0,0,true),"queue without Execute accepted");
    require(!ObservedLastUse(7,7,true),"previous recording accepted");
    require(!ObservedLastUse(7,6,true),"old observation accepted");
    require(!ObservedLastUse(7,8,false),"submission without owner accepted");
    require(ObservedLastUse(7,8,true),"real later submission rejected");
    struct Batch{bool submitted;uint64_t completed,target;};
    std::vector<Batch> batches{{false,9,8},{true,8,8},{true,UINT64_MAX,8},{true,7,8}};
    unsigned released=0;
    CollectReady(batches,[](const Batch& b){return b.submitted && FenceReached(b.completed,b.target);},[&](Batch&){++released;});
    require(released==1 && batches.size()==3,"unsubmitted/incomplete/device-lost batch released");
    D3D12_RESOURCE_DESC src{};src.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;src.Width=1920;src.Height=1080;
    src.DepthOrArraySize=1;src.MipLevels=1;src.SampleDesc.Count=1;src.Format=DXGI_FORMAT_R32_TYPELESS;
    auto clone=src;clone.Format=DXGI_FORMAT_R32_FLOAT;
    const auto fits=[&](const auto& d){return DlssNr::GuideCopyCompatible(clone,d,DXGI_FORMAT_R32_FLOAT);};
    require(fits(src),"matching guide rejected");
    unsigned rejected=0;
    for(int k=0;k<7;++k){auto changed=src;switch(k){case 0:changed.Width=1280;break;case 1:changed.Height=720;break;
        case 2:changed.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;break;case 3:changed.DepthOrArraySize=2;break;
        case 4:changed.MipLevels=2;break;case 5:changed.SampleDesc.Count=2;break;case 6:changed.SampleDesc.Quality=1;break;}
        require(!fits(changed),"incompatible guide accepted");++rejected;
    }
    require(!DlssNr::GuideCopyCompatible(clone,src,DXGI_FORMAT_R16_FLOAT),"wrong format accepted");
    require(!DlssNr::RetainedResourcesCompatible(false,false,true),"changed guide copied during locked rebuild");
    require(!DlssNr::RetainedResourcesCompatible(true,false,false),"changed output copied during locked rebuild");
    require(!DlssNr::RetainedResourcesCompatible(false,true,false),"changed format copied during locked rebuild");
    require(DlssNr::RetainedResourcesCompatible(false,false,false),"compatible old tuning blocked");
    std::printf("PASS retirement evidence and guide contracts: %u shape mismatches + format + locked fallback\n",rejected);
}
void snapshotTests(){
    struct Value{uint64_t frame=0,inverse=UINT64_MAX;std::array<char,32> reason{};};
    DlssNr::PublishedSnapshot<Value> snapshot;
    std::atomic<bool> start=false,done=false,broken=false;std::atomic<uint64_t> reads=0;uint64_t published=0;
    std::array<std::thread,3> readers;
    for(auto& t:readers)t=std::thread([&]{while(!start.load())std::this_thread::yield();do{auto v=snapshot.Read();
        if(v.inverse!=~v.frame)broken=true;++reads;}while(!done.load());});
    const auto begin=std::chrono::steady_clock::now();start=true;
    for(uint64_t i=1;i<=200000;++i){Value v;v.frame=i;v.inverse=~i;if(snapshot.TryPublish(v))++published;}
    done=true;for(auto& t:readers)t.join();
    require(!broken && reads>0 && published>0,"snapshot torn or no concurrent progress");
    Value original;original.frame=51;original.inverse=~uint64_t(51);original.reason[0]='A';
    require(snapshot.TryPublish(original),"idle publish failed");original.reason[0]='B';
    require(snapshot.Read().reason[0]=='A',"snapshot aliases writer data");
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    std::printf("PASS snapshot: 200000 publication attempts, %llu published, %llu reads, %.2f ms stress (not game performance)\n",
        static_cast<unsigned long long>(published),static_cast<unsigned long long>(reads.load()),ms);
}
ComPtr<ID3D12Resource> resource(ID3D12Device* d,const D3D12_RESOURCE_DESC& desc,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
    D3D12_HEAP_PROPERTIES props{};props.Type=type;ComPtr<ID3D12Resource> r;
    check(d->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)));return r;
}
void readyCapture(ID3D12Device* d,capture::FrameCapture& cap){
    ComPtr<ID3D12CommandQueue> q;D3D12_COMMAND_QUEUE_DESC qd{};check(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator> a;check(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList> c;check(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&c)));
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=16;td.Height=16;td.DepthOrArraySize=1;
    td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    auto tex=resource(d,td,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;d->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.DepthOrArraySize=1;
    bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto upload=resource(d,bd,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    unsigned char* mem{};D3D12_RANGE empty{};check(upload->Map(0,&empty,reinterpret_cast<void**>(&mem)));
    std::memset(mem,0,static_cast<size_t>(bytes));
    const std::array<float,4> pixel{.25f,.5f,.75f,1.f};
    for(UINT y=0;y<16;++y)for(UINT x=0;x<16;++x)std::memcpy(mem+y*fp.Footprint.RowPitch+x*16,pixel.data(),16);
    upload->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=tex.Get();c->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={tex.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&barrier);
    capture::FrameEvidence ev{};ev.frame=100;ev.successfulSinceReset=50;ev.inputWidth=ev.inputHeight=ev.networkWidth=ev.networkHeight=16;
    ev.rects.output=ev.rects.depth=ev.rects.motion={0,0,16,16};ev.resolve.Width=ev.resolve.Height=16;ev.resolve.WhitePoint=1;
    cap.request(1);cap.record(c.Get(),d,tex.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,tex.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,
        tex.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,tex.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,&ev);
    check(c->Close());ID3D12CommandList* lists[]{c.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;check(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));check(q->Signal(fence.Get(),1));
    HANDLE event=CreateEvent(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"event");check(fence->SetEventOnCompletion(1,event));
    require(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"WARP completion timeout");CloseHandle(event);
    require(cap.readyToWrite(),"capture not ready");
}
std::wstring failTarget,currentFile;int failKind=0;
std::FILE* faultOpen(const wchar_t* path,const wchar_t* mode){currentFile=std::filesystem::path(path).filename().wstring();
    if(failKind==1 && currentFile==failTarget)return nullptr;return _wfopen(path,mode);}
size_t faultWrite(const void* p,size_t size,size_t count,std::FILE* f){
    return std::fwrite(p,size,failKind==2 && currentFile==failTarget?count/2:count,f);}
int faultClose(std::FILE* f){const int r=std::fclose(f);return failKind==3 && currentFile==failTarget?EOF:r;}
int faultError(std::FILE* f){return failKind==4 && currentFile==failTarget?1:std::ferror(f);}
void captureTests(const std::filesystem::path& output){
    ComPtr<ID3D12Debug> debug;check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> info;check(device.As(&info));
    struct Case{const char* name;const wchar_t* target;int kind;const char* reason;};
    const Case cases[]{
        {"success",L"",0,""},{"raw_open",L"after_00.raw",1,"raw_write"},
        {"raw_short",L"before_00.raw",2,"raw_write"},{"raw_close",L"before_00.raw",3,"raw_write"},
        {"metadata_open",L"evidence_00.json",1,"metadata_write"},{"metadata_close",L"evidence_00.json",3,"metadata_write"},
        {"metadata_error",L"evidence_00.json",4,"metadata_write"},{"manifest_open",L"manifest.pending",1,"manifest_write"},
        {"manifest_close",L"manifest.pending",3,"manifest_write"},{"directory_invalid",L"",0,"directory_create"}};
    capture::FileIo io{faultOpen,faultWrite,faultError,faultClose};
    for(const auto& test:cases){
        const auto dir=output/test.name;
        if(std::string(test.name)=="directory_invalid"){std::ofstream file(dir);file<<"not a directory";}
        else {std::filesystem::create_directories(dir);std::ofstream stale(dir/"manifest.txt");stale<<"old";}
        capture::FrameCapture cap;readyCapture(device.Get(),cap);failTarget=test.target;failKind=test.kind;
        const auto result=cap.write(dir,io);const bool success=test.reason[0]==0;
        require(result.state==(success?capture::WriteResult::State::Success:capture::WriteResult::State::Failed),"incorrect capture write status");
        require(std::string(result.reason)==test.reason,"incorrect failure reason");
        require(!cap.isActive() && !cap.readyToWrite(),"completed capture not released");
        if(std::string(test.name)!="directory_invalid")require(std::filesystem::exists(dir/"manifest.txt")==success,"invalid completion marker");
        if(success){require(std::filesystem::file_size(dir/"before_00.raw")==4096,"raw length");
            require(std::filesystem::exists(dir/"evidence_00.json"),"metadata missing");}
        std::printf("PASS capture %s: %s\n",test.name,success?"complete":result.reason);
    }
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n=0;check(info->GetMessage(i,nullptr,&n));std::vector<unsigned char> storage(n);
        auto* msg=reinterpret_cast<D3D12_MESSAGE*>(storage.data());check(info->GetMessage(i,msg,&n));
        if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::printf("D3D12: %s\n",msg->pDescription);throw std::runtime_error("D3D12 debug error");}}
    std::puts("PASS WARP debug layer: no errors/corruption; no NR model or game loaded");
}
int wmain(int argc,wchar_t** argv){try{require(argc==2,"output directory required");std::filesystem::create_directories(argv[1]);
    policyTests();snapshotTests();captureTests(argv[1]);return 0;}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}}
