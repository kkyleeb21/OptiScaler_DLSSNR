// Real DX12/WARP test: copies are never readable on frame age or a queue hint.
#include "../../OptiScaler/dlssnr/CaptureSubmission.h"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cassert>
#include <cstdio>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("DX12 test setup failed"); }
int main()
{
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> warp;
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queues[2];
    ComPtr<ID3D12CommandAllocator> allocators[2];
    ComPtr<ID3D12GraphicsCommandList> lists[2];
    for (int i = 0; i < 2; ++i)
    {
        D3D12_COMMAND_QUEUE_DESC desc {};
        Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queues[i])));
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])));
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i].Get(),
                                       nullptr, IID_PPV_ARGS(&lists[i])));
        Check(lists[i]->Close());
    }
    capture::SubmissionBatch batch;
    assert(batch.arm(device.Get(), lists[0].Get()));
    assert(batch.arm(device.Get(), lists[1].Get()));
    assert(!batch.complete());
    ID3D12CommandList* first[] = { lists[0].Get() };
    queues[0]->ExecuteCommandLists(1, first);
    batch.submitted(queues[0].Get(), 1, first);
    assert(!batch.complete()); // another queue's recorded work has not been submitted
    ID3D12CommandList* second[] = { lists[1].Get() };
    queues[1]->ExecuteCommandLists(1, second);
    batch.submitted(queues[1].Get(), 1, second);
    const auto deadline = GetTickCount64() + 5000;
    while (!batch.complete() && GetTickCount64() < deadline) Sleep(1);
    assert(batch.complete());
    batch.clearCompleted();
    assert(!batch.complete());
    // Reused list does not inherit previous submission proof.
    assert(batch.arm(device.Get(), lists[0].Get()));
    assert(!batch.complete());
    for (int i = 1; i < 8; ++i) assert(batch.arm(device.Get(), lists[0].Get()));
    assert(!batch.arm(device.Get(), lists[0].Get())); // bounded capture capacity
    queues[0]->ExecuteCommandLists(1, first);
    batch.submitted(queues[0].Get(), 1, first);
    const auto secondDeadline = GetTickCount64() + 5000;
    while (!batch.complete() && GetTickCount64() < secondDeadline) Sleep(1);
    assert(batch.complete());
    batch.clearCompleted();
    std::puts("PASS DX12 WARP: unsent, two queues, completion, reused list, bounded capacity.");
}
