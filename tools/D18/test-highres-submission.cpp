// Test the new five-slot admission rule with actual blocked D3D12 submissions.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cassert>
#include <iostream>
#define LOG_INFO(...) ((void)0)
namespace DlssNr {bool EnsureNativeSubmissionObserver(ID3D12Device*){return true;}}
#pragma warning(push)
#pragma warning(disable:4100) // Existing fixture deliberately replaces logging with a no-op.
#include <dlssnr/Submission.h>
#pragma warning(pop)
using Microsoft::WRL::ComPtr;
using namespace DlssNr::Submission;
#include <array>
#include <dlssnr/HighResolutionNr.h>
#include <dlssnr/CaptureEvidence.h>
int main(){
 ComPtr<IDXGIFactory4> f;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f))));
 ComPtr<IDXGIAdapter> a;assert(SUCCEEDED(f->EnumWarpAdapter(IID_PPV_ARGS(&a))));
 ComPtr<ID3D12Device> d;assert(SUCCEEDED(D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d))));
 ComPtr<ID3D12CommandQueue> q;D3D12_COMMAND_QUEUE_DESC qd{};assert(SUCCEEDED(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))));
 ComPtr<ID3D12CommandAllocator> alloc;assert(SUCCEEDED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc))));
 ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 ComPtr<ID3D12Fence> gate;assert(SUCCEEDED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))));assert(SUCCEEDED(q->Wait(gate.Get(),1)));
 std::array<Token,32> slots{};ID3D12CommandList* lists[]={list.Get()};unsigned first=0;
 for(unsigned frame=0;frame<6;++frame){
    assert(DlssNr::HighResolution::SlotsReady(slots,first,5));auto use=Track(q.Get(),list.Get());assert(use && !use->submitted.load());
    for(unsigned i=0;i<5;++i)slots[(first+i)%32]=use;first=(first+5)%32;
    q->ExecuteCommandLists(1,lists);NotifySubmitted(q.Get(),1,lists);
    assert(use->submitted.load() && !use->Complete());
 }
 assert(first==30);assert(!DlssNr::HighResolution::SlotsReady(slots,first,5));
 assert(!DlssNr::HighResolution::SlotsReady(slots,0,1)); // Ordinary mode cannot recycle high-mode descriptors either.
 assert(SUCCEEDED(gate->Signal(1)));auto last=slots[29];HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(done);
 assert(SUCCEEDED(last->fence->SetEventOnCompletion(1,done)));assert(WaitForSingleObject(done,10000)==WAIT_OBJECT_0);CloseHandle(done);
 assert(DlssNr::HighResolution::SlotsReady(slots,first,5));assert(DlssNr::HighResolution::SlotsReady(slots,0,32));
 assert(!DlssNr::HighResolution::SlotsReady(slots,0,33));
 assert(!DlssNr::HighResolution::Budget(UINT64_MAX,1,UINT64_MAX,{4800,2704}));
 capture::FrameEvidence e{};e.inputWidth=4800;e.inputHeight=2704;e.networkWidth=4800;e.networkHeight=2704;
 e.rects.color=e.rects.output={0,0,3840,2160};e.rects.depth=e.rects.motion={0,0,1920,1080};e.resolve.RelativeColour=1;
 FILE* out=nullptr;assert(fopen_s(&out,"highres-capture-metadata.json","w")==0);capture::writeEvidence(out,e);fclose(out);
 std::cout<<"PASS high-resolution five-slot ring wrap: submitted is not complete; blocked slots retained; completion enables reuse; overflow budget rejected; capture metadata written\n";
}
