#pragma once

#include <d3d12.h>
#include <memory>
#include <mutex>
#include <vector>

namespace capture
{
// One fence per captured list use, signalled only by the existing post-ExecuteCommandLists observer.
// No swapchain/queue hint and no elapsed-frame heuristic can make a readback ready.
class SubmissionBatch
{
    struct Point
    {
        ID3D12Fence* fence = nullptr;
        ID3D12CommandList* list = nullptr;
        bool submitted = false;
        bool failed = false;
        ~Point() { if (fence) fence->Release(); if (list) list->Release(); }
    };
    std::mutex mutex_;
    std::vector<std::unique_ptr<Point>> points_;

  public:
    bool arm(ID3D12Device* device, ID3D12GraphicsCommandList* list)
    {
        std::lock_guard lock(mutex_);
        if (points_.size() >= 8) return false;
        auto point = std::make_unique<Point>();
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&point->fence))))
            return false;
        point->list = list;
        list->AddRef();
        points_.push_back(std::move(point));
        return true;
    }

    void submitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
    {
        std::lock_guard lock(mutex_);
        for (auto& point : points_)
        {
            if (point->submitted || point->failed) continue;
            for (UINT i = 0; i < count; ++i)
                if (lists[i] == point->list)
                {
                    const HRESULT result = queue->Signal(point->fence, 1);
                    point->failed = FAILED(result);
                    point->submitted = SUCCEEDED(result);
                    break;
                }
        }
    }

    bool complete()
    {
        std::lock_guard lock(mutex_);
        if (points_.empty()) return false;
        for (const auto& point : points_)
        {
            if (point->failed || !point->submitted) return false;
            const UINT64 done = point->fence->GetCompletedValue();
            if (done == UINT64_MAX || done < 1) return false; // device removal is not completion
        }
        return true;
    }

    void clearCompleted()
    {
        // Only the render owner may clear, after complete() and after consuming the readbacks.
        std::lock_guard lock(mutex_);
        points_.clear();
    }
};
}
