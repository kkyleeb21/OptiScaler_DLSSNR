#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <cassert>
#include <cstdio>
#include "../../OptiScaler/dlssnr/RetirementPolicy.h"
#pragma comment(lib,"d3d12.lib")
using Microsoft::WRL::ComPtr;
struct Batch { ComPtr<ID3D12Fence> fence; uint64_t target; };
int main() {
    ComPtr<ID3D12Device> device;
    if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return 1;
    ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC desc{};
    if(FAILED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue))))return 2;
    ComPtr<ID3D12Fence> fence;
    if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return 3;
    std::vector<Batch> retired{{fence,1}}; int released=0;
    auto ready=[](const Batch& b){return b.fence && DlssNr::Retirement::FenceReached(b.fence->GetCompletedValue(),b.target);};
    auto release=[&](Batch&){++released;};
    assert(DlssNr::Retirement::CollectReady(retired,ready,release)==0 && released==0);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return 4;
    const bool completed=SUCCEEDED(fence->SetEventOnCompletion(1,event)) && SUCCEEDED(queue->Signal(fence.Get(),1)) && WaitForSingleObject(event,2000)==WAIT_OBJECT_0;
    CloseHandle(event);if(!completed)return 5;
    assert(DlssNr::Retirement::CollectReady(retired,ready,release)==1 && released==1 && retired.empty());
    assert(DlssNr::Retirement::CollectReady(retired,ready,release)==0 && released==1);
    puts("PASS: real DX12 queue/fence pending -> completed collection without NR dispatch; one release");
}
