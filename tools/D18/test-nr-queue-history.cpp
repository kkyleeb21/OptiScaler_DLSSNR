#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cassert>
#include <iostream>
#define LOG_INFO(...) ((void)0)
bool hookReady=true;
namespace DlssNr {bool EnsureNativeSubmissionObserver(ID3D12Device*){return hookReady;}}
#pragma warning(push)
#pragma warning(disable:4100)
#include <dlssnr/Submission.h>
#pragma warning(pop)
using Microsoft::WRL::ComPtr;
using namespace DlssNr::Submission;
int main(){
 ComPtr<IDXGIFactory4> factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
 ComPtr<IDXGIAdapter> adapter;assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
 ComPtr<ID3D12Device> device;assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
 auto makeQueue=[&]{ComPtr<ID3D12CommandQueue> q;D3D12_COMMAND_QUEUE_DESC d{};assert(SUCCEEDED(device->CreateCommandQueue(&d,IID_PPV_ARGS(&q))));return q;};
 auto execution=makeQueue(),display=makeQueue();
 ComPtr<ID3D12CommandAllocator> allocator;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
 auto makeList=[&]{ComPtr<ID3D12GraphicsCommandList> l;assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&l))));assert(SUCCEEDED(l->Close()));return l;};
 auto submit=[&](ID3D12CommandQueue* q,ID3D12GraphicsCommandList* l){ID3D12CommandList* lists[]={l};q->ExecuteCommandLists(1,lists);NotifySubmitted(q,1,lists);};
 auto finish=[&](ID3D12CommandQueue* q){ComPtr<ID3D12Fence> f;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))));assert(SUCCEEDED(q->Signal(f.Get(),1)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);assert(SUCCEEDED(f->SetEventOnCompletion(1,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);};
 const char* reason=nullptr;bool promoted=false;
 auto unknown=makeList();assert(!ResolveSrQueue(display.Get(),unknown.Get(),&reason,&promoted));
 assert(std::string(reason)=="queue_unknown_list"&&!promoted);
 // Key regression: Execute happened before this list was ever identified as SR.
 auto rotating=makeList();submit(execution.Get(),rotating.Get());finish(execution.Get());
 assert(!srLists.contains(rotating.Get()));assert(recentExecutions.contains(rotating.Get()));
 assert(ResolveSrQueue(display.Get(),rotating.Get(),&reason,&promoted).Get()==execution.Get());
 assert(promoted&&std::string(reason).empty()&&!recentExecutions.contains(rotating.Get()));
 assert(ResolveSrQueue(display.Get(),rotating.Get(),&reason,&promoted).Get()==execution.Get()&&!promoted);
 // Historical Execute never marks newly recorded work submitted or completed.
 auto ticket=Track(execution.Get(),rotating.Get());assert(ticket&&!ticket->submitted.load()&&!ticket->Complete());
 ComPtr<ID3D12Fence> gate;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))));
 assert(SUCCEEDED(execution->Wait(gate.Get(),1)));submit(execution.Get(),rotating.Get());
 assert(ticket->submitted.load()&&!ticket->Complete());assert(SUCCEEDED(gate->Signal(1)));finish(execution.Get());assert(ticket->Complete());
 // Same pinned list seen on a second queue remains quarantined, even after return to owner.
 auto ambiguous=makeList();submit(execution.Get(),ambiguous.Get());finish(execution.Get());
 submit(display.Get(),ambiguous.Get());finish(display.Get());
 assert(!ResolveSrQueue(display.Get(),ambiguous.Get(),&reason,&promoted));assert(std::string(reason)=="queue_ambiguous");
 submit(execution.Get(),ambiguous.Get());finish(execution.Get());
 assert(!ResolveSrQueue(display.Get(),ambiguous.Get(),&reason,&promoted));
 // Keep old non-owner freshness rules; do not turn recent history into permanent authority.
 auto stale=makeList();submit(display.Get(),stale.Get());finish(display.Get());
 recentExecutions.at(stale.Get()).tick=0;
 assert(!ResolveSrQueue(display.Get(),stale.Get(),&reason,&promoted));assert(std::string(reason)=="queue_history_stale");
 auto invalid=makeList();submit(execution.Get(),invalid.Get());finish(execution.Get());
 recentExecutions.at(invalid.Get()).device.Reset();
 assert(!ResolveSrQueue(display.Get(),invalid.Get(),&reason,&promoted));assert(std::string(reason)=="queue_device_mismatch");
 hookReady=false;assert(!ResolveSrQueue(display.Get(),rotating.Get(),&reason,&promoted));assert(std::string(reason)=="queue_observer_unavailable");hookReady=true;
 // Cache pressure evicts evidence, never the pinned SR map or pending ticket.
 auto oldest=makeList();submit(execution.Get(),oldest.Get());
 for(size_t i=0;i<MaxRecentExecutions+1;++i){auto l=makeList();submit(execution.Get(),l.Get());}
 finish(execution.Get());assert(recentExecutions.size()==MaxRecentExecutions);
 assert(!recentExecutions.contains(oldest.Get()));
 assert(!ResolveSrQueue(display.Get(),oldest.Get(),&reason,&promoted));assert(std::string(reason)=="queue_unknown_list");
 assert(ResolveSrQueue(display.Get(),rotating.Get()).Get()==execution.Get());
 assert(ticket->Complete());
 // No raw diagnostic-only address may be promoted.
 auto rawOnly=makeList();observed[rawOnly.Get()]={reinterpret_cast<uintptr_t>(execution.Get()),GetTickCount64()};
 assert(!ResolveSrQueue(display.Get(),rawOnly.Get(),&reason,&promoted));assert(std::string(reason)=="queue_unknown_list");
 while(srLists.size()<256){auto l=makeList();assert(!ResolveSrQueue(display.Get(),l.Get()));}
 auto excess=makeList();assert(!ResolveSrQueue(display.Get(),excess.Get(),&reason,&promoted));assert(std::string(reason)=="queue_sr_list_limit");
 ClearRecentExecutionHistory();assert(recentExecutions.empty());assert(ticket->Complete());
 std::cout<<"PASS production pinned Execute history -> first SR resolution; display queue ignored; unknown/ambiguous/stale/device/hook/raw-only/cache-pressure guarded; actual blocked fence still required; WARP empty lists only\n";
}
