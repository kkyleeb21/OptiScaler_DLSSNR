#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cassert>
#include <iostream>
#define LOG_INFO(...) ((void)0)
namespace DlssNr {bool EnsureNativeSubmissionObserver(ID3D12Device*){return true;}}
#include "../../OptiScaler/dlssnr/Submission.h"
using Microsoft::WRL::ComPtr;
using namespace DlssNr::Submission;
int main(){
 ComPtr<IDXGIFactory4> f;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f))));
 ComPtr<IDXGIAdapter> a;assert(SUCCEEDED(f->EnumWarpAdapter(IID_PPV_ARGS(&a))));
 ComPtr<ID3D12Device> d;assert(SUCCEEDED(D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d))));
 ComPtr<ID3D12CommandQueue> q,q2;D3D12_COMMAND_QUEUE_DESC qd{};assert(SUCCEEDED(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))));assert(SUCCEEDED(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q2))));
 ComPtr<ID3D12CommandAllocator> alloc;assert(SUCCEEDED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc))));
 ComPtr<ID3D12GraphicsCommandList> l;assert(SUCCEEDED(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&l))));assert(SUCCEEDED(l->Close()));
 assert(!ResolveSrQueue(q.Get(),l.Get()));
 ID3D12CommandList* lists[]={l.Get()};q->ExecuteCommandLists(1,lists);NotifySubmitted(q.Get(),1,lists);
 assert(ResolveSrQueue(q.Get(),l.Get()).Get()==q.Get());
 auto token=Track(q.Get(),l.Get());assert(token && !token->Complete());
 // Old but pinned current-owner history is valid; a new ticket must still await submission.
 { std::lock_guard lock(mutex); srLists.at(l.Get()).tick=GetTickCount64()-7600; }
 assert(ResolveSrQueue(q.Get(),l.Get()).Get()==q.Get());
 assert(!token->Complete());
 assert(!Track(q2.Get(),l.Get()));
 // A ticket stays pending until the containing list is actually submitted.
 q->ExecuteCommandLists(1,lists);NotifySubmitted(q.Get(),1,lists);assert(token->submitted.load());
 HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);assert(SUCCEEDED(token->fence->SetEventOnCompletion(1,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);assert(token->Complete());
 q2->ExecuteCommandLists(1,lists);NotifySubmitted(q2.Get(),1,lists);assert(!ResolveSrQueue(q.Get(),l.Get()));
 std::cout<<"PASS WARP native post-submit tickets: no queue guess, pending before submission, actual Execute+Signal completion, fixed owner, ambiguous list rejected\n";
}
