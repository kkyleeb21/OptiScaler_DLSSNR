#pragma once
#include <nvsdk_ngx_defs.h>
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>

// Adapted from wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass, 5019c979 (GPL-3.0).
// Reads never insert. An FG count avoids per-evaluate scans of all handles.
// Never hold this mutex across runtime calls, rendering, or GPU waits.
class NgxFeatureRegistry
{
    mutable std::mutex mutex_;
    std::unordered_map<unsigned int, NVSDK_NGX_Feature> features_;
    size_t frameGenerationCount_ = 0;

  public:
    struct Snapshot
    {
        std::optional<NVSDK_NGX_Feature> feature;
        bool frameGenerationCreated = false;
        bool IsUpscaler() const
        {
            return feature == NVSDK_NGX_Feature_SuperSampling ||
                   feature == NVSDK_NGX_Feature_RayReconstruction;
        }
    };
    void Record(unsigned int handle, NVSDK_NGX_Feature feature)
    {
        std::lock_guard lock(mutex_);
        if (auto it = features_.find(handle); it != features_.end())
        {
            if (it->second == NVSDK_NGX_Feature_FrameGeneration) --frameGenerationCount_;
            it->second = feature;
        }
        else
        {
            features_.emplace(handle, feature);
        }
        if (feature == NVSDK_NGX_Feature_FrameGeneration) ++frameGenerationCount_;
    }
    void RecordCreated(NVSDK_NGX_Result result, const NVSDK_NGX_Handle* handle, NVSDK_NGX_Feature feature)
    {
        if (result == NVSDK_NGX_Result_Success && handle) Record(handle->Id, feature);
    }
    void ForgetReleased(NVSDK_NGX_Result result, unsigned int handle)
    {
        if (result != NVSDK_NGX_Result_Success) return;
        std::lock_guard lock(mutex_);
        if (auto it = features_.find(handle); it != features_.end())
        {
            if (it->second == NVSDK_NGX_Feature_FrameGeneration) --frameGenerationCount_;
            features_.erase(it);
        }
    }
    Snapshot Read(unsigned int handle) const
    {
        std::lock_guard lock(mutex_);
        Snapshot result;
        if (auto it = features_.find(handle); it != features_.end()) result.feature = it->second;
        result.frameGenerationCreated = frameGenerationCount_ != 0;
        return result;
    }
    void Clear()
    {
        std::lock_guard lock(mutex_);
        features_.clear();
        frameGenerationCount_ = 0;
    }
};
