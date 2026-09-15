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
#include <dlssnr/MultipassPolicy.h>
#include <dlssnr/CaptureEvidence.h>
int main(){
 ComPtr<IDXGIFactory4> f;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f))));
 ComPtr<IDXGIAdapter> a;assert(SUCCEEDED(f->EnumWarpAdapter(IID_PPV_ARGS(&a))));
 ComPtr<ID3D12Device> d;assert(SUCCEEDED(D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d))));
 ComPtr<ID3D12CommandQueue> q;D3D12_COMMAND_QUEUE_DESC qd{};assert(SUCCEEDED(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))));
 ComPtr<ID3D12CommandAllocator> alloc;assert(SUCCEEDED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc))));
 ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 ComPtr<ID3D12Fence> gate;assert(SUCCEEDED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))));assert(SUCCEEDED(q->Wait(gate.Get(),1)));
 std::array<Token,DlssNr::Multipass::DescriptorSlots> slots{};ID3D12CommandList* lists[]={list.Get()};unsigned first=0;
 for(unsigned frame=0;frame<5;++frame){
    assert(DlssNr::HighResolution::SlotsReady(slots,first,12));auto use=Track(q.Get(),list.Get());assert(use && !use->submitted.load());
    for(unsigned i=0;i<12;++i)slots[(first+i)%64]=use;first=(first+12)%64;
    q->ExecuteCommandLists(1,lists);NotifySubmitted(q.Get(),1,lists);
    assert(use->submitted.load() && !use->Complete());
 }
 assert(first==60);assert(!DlssNr::HighResolution::SlotsReady(slots,first,12));
 assert(!DlssNr::HighResolution::SlotsReady(slots,0,1)); // Ordinary mode cannot recycle high-mode descriptors either.
 assert(SUCCEEDED(gate->Signal(1)));auto last=slots[59];HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(done);
 assert(SUCCEEDED(last->fence->SetEventOnCompletion(1,done)));assert(WaitForSingleObject(done,10000)==WAIT_OBJECT_0);CloseHandle(done);
 assert(DlssNr::HighResolution::SlotsReady(slots,first,12));assert(DlssNr::HighResolution::SlotsReady(slots,0,64));
 assert(!DlssNr::HighResolution::SlotsReady(slots,0,65));
 assert(!DlssNr::HighResolution::Budget(UINT64_MAX,1,UINT64_MAX,{4800,2704}));
 assert(DlssNr::Multipass::Slots(4)+1==12);
 for(unsigned n=1;n<=4;++n){assert(DlssNr::Multipass::Count(n,false)==n);assert(DlssNr::Multipass::Count(n,true)==1);}
 assert(DlssNr::Multipass::Count(99,false)==4);
 assert(DlssNr::Multipass::Network(3840,.5f,16)==1920);
 assert(DlssNr::Multipass::Network(2160,.5f,8)==1080);
 assert(DlssNr::Multipass::Sanitize({true,true,NAN,INFINITY}).ratio==.5f);
 std::cout<<"PASS four-pass shared twelve-slot ring wrap: submitted is not complete; blocked slots retained; completion enables reuse; overflow budget rejected; five complete four-pass recordings retained\n";
}
